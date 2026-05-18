#pragma once
// ═══════════════════════════════════════════════════════════
//  OhmCollector.hpp  v3
//
//  Читает ВСЕ данные из OpenHardwareMonitor через WMI:
//    ROOT\OpenHardwareMonitor → Sensor / Hardware
//
//  Собирает:
//    CPU  — температура (Package / Tdie / среднее ядер)
//    CPU  — нагрузка (CPU Total)
//    GPU  — температура (NVIDIA / AMD)
//    GPU  — нагрузка (GPU Core)
//    PSU  — напряжения 12V / 5V / 3.3V
//
//  ВАЖНО:
//    - CoInitializeEx / CoInitializeSecurity вызываются ТОЛЬКО
//      из main.cpp ДО создания OhmCollector. Здесь — не трогаем.
//    - OHM ищется рядом с exe (не в подпапке).
//    - При старте пишет все найденные сенсоры в C:\ohm_sensors.log
// ═══════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <WbemIdl.h>
#include <comdef.h>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// ── Структуры результата ────────────────────────────────────

struct PsuVoltages {
    float v12   = 0.0f;
    float v5    = 0.0f;
    float v33   = 0.0f;
    bool  valid = false;
};

struct OhmData {
    float cpuTemp = 0.0f;
    float cpuLoad = 0.0f;
    float gpuTemp = 0.0f;
    float gpuLoad = 0.0f;
    PsuVoltages   psu;
    std::string   cpuName;
    std::string   gpuName;
};

// ═══════════════════════════════════════════════════════════

class OhmCollector {
public:
    // ohmExeName — имя exe рядом с агентом (или подпапка\имя)
    explicit OhmCollector(const std::string& ohmExeName = "OpenHardwareMonitor.exe") {
        char exeDir[MAX_PATH] = {};
        GetModuleFileNameA(NULL, exeDir, MAX_PATH);
        std::string dir(exeDir);
        dir = dir.substr(0, dir.find_last_of("\\/"));
        m_ohmFullPath = dir + "\\" + ohmExeName;
        std::cout << "[OHM] exe path: " << m_ohmFullPath << "\n";
    }

    ~OhmCollector() = default;

    // ── Init: запустить OHM, дождаться WMI, залогировать сенсоры
    // COM уже инициализирован снаружи (main.cpp) — здесь не трогаем.
    bool Init() {
        // Запускаем OHM если не запущен
        EnsureOhmRunning();

        // Ждём дольше — до 20 сек
        std::vector<Sensor> sensors;
        for (int i = 0; i < 40; i++) {
            Sleep(500);
            sensors = QueryAllSensors();
            // Проверяем что значения не нулевые
            bool hasData = false;
            for (auto& s : sensors)
                if (s.value > 0) { hasData = true; break; }
            if (hasData) {
                std::cout << "[OHM] Ready after " << (i+1)*500
                     << "ms, sensors: " << sensors.size() << "\n";
                break;
            }
        }

        if (sensors.empty()) {
            std::cout << "[OHM] Нет данных\n";
            return false;
        }

        RefreshHardwareNames();
        DumpSensors(sensors);
        return true;
    }

    // ── Watchdog: вызывать раз в ~45 сек ────────────────────
    void Watchdog() {
        if (!IsOhmRunning()) {
            std::cout << "[OHM][WARNING] OHM died, restarting...\n";
            EnsureOhmRunning();
            Sleep(4000);
        }
    }

    // ── Собрать все данные одним WMI-запросом ───────────────
    OhmData Collect() {
        OhmData d;
        d.cpuName = m_cpuName;
        d.gpuName = m_gpuName;

        auto sensors = QueryAllSensors();
        if (sensors.empty()) return d;

        if (m_cpuName.empty() || m_gpuName.empty())
            RefreshHardwareNames();
        d.cpuName = m_cpuName;
        d.gpuName = m_gpuName;

        // ── CPU температура ──────────────────────────────────
        // Приоритет: CPU Package → CCD/Tdie/Tctl → среднее CPU Core #N → любой CPU temp
        {
            float coreSum = 0; int coreCnt = 0;
            bool  found   = false;

            for (auto& s : sensors) {
                if (s.sensorType != L"Temperature") continue;
                std::wstring nl = toLower(s.name);
                if (nl == L"cpu package" && inRange(s.value, 5, 120)) {
                    d.cpuTemp = s.value; found = true; break;
                }
            }
            if (!found) {
                for (auto& s : sensors) {
                    if (s.sensorType != L"Temperature") continue;
                    std::wstring nl = toLower(s.name);
                    if ((nl.find(L"ccd")  != std::wstring::npos ||
                         nl.find(L"tdie") != std::wstring::npos ||
                         nl.find(L"tctl") != std::wstring::npos) &&
                        inRange(s.value, 5, 120)) {
                        d.cpuTemp = s.value; found = true; break;
                    }
                }
            }
            if (!found) {
                for (auto& s : sensors) {
                    if (s.sensorType != L"Temperature") continue;
                    std::wstring nl = toLower(s.name);
                    if ((nl.find(L"cpu core") != std::wstring::npos ||
                         nl.find(L"core #")   != std::wstring::npos) &&
                        inRange(s.value, 5, 120)) {
                        coreSum += s.value; coreCnt++;
                    }
                }
                if (coreCnt > 0) { d.cpuTemp = coreSum / coreCnt; found = true; }
            }
            if (!found) {
                for (auto& s : sensors) {
                    if (s.sensorType != L"Temperature") continue;
                    std::wstring pl = toLower(s.parent);
                    if (pl.find(L"cpu") != std::wstring::npos &&
                        inRange(s.value, 5, 120)) {
                        d.cpuTemp = s.value; break;
                    }
                }
            }
        }

        // ── CPU нагрузка ─────────────────────────────────────
        {
            bool found = false;
            for (auto& s : sensors) {
                if (s.sensorType != L"Load") continue;
                std::wstring nl = toLower(s.name);
                if (nl == L"cpu total" ||
                    nl.find(L"cpu total") != std::wstring::npos) {
                    d.cpuLoad = clamp100(s.value); found = true; break;
                }
            }
            if (!found) {
                float sum = 0; int cnt = 0;
                for (auto& s : sensors) {
                    if (s.sensorType != L"Load") continue;
                    std::wstring nl = toLower(s.name);
                    if (nl.find(L"cpu core") != std::wstring::npos)
                        { sum += s.value; cnt++; }
                }
                if (cnt > 0) d.cpuLoad = clamp100(sum / cnt);
            }
        }

        // ── GPU температура ──────────────────────────────────
        {
            bool found = false;
            for (auto& s : sensors) {
                if (s.sensorType != L"Temperature") continue;
                std::wstring nl = toLower(s.name);
                if (nl == L"gpu core" && inRange(s.value, 5, 120)) {
                    d.gpuTemp = s.value; found = true; break;
                }
            }
            if (!found) {
                for (auto& s : sensors) {
                    if (s.sensorType != L"Temperature") continue;
                    std::wstring nl = toLower(s.name);
                    if (nl.find(L"gpu") != std::wstring::npos &&
                        inRange(s.value, 5, 120)) {
                        d.gpuTemp = s.value; break;
                    }
                }
            }
        }

        // ── GPU нагрузка ─────────────────────────────────────
        {
            bool found = false;
            for (auto& s : sensors) {
                if (s.sensorType != L"Load") continue;
                std::wstring nl = toLower(s.name);
                if (nl == L"gpu core") {
                    d.gpuLoad = clamp100(s.value); found = true; break;
                }
            }
            if (!found) {
                for (auto& s : sensors) {
                    if (s.sensorType != L"Load") continue;
                    std::wstring nl = toLower(s.name);
                    if (nl.find(L"gpu") != std::wstring::npos) {
                        d.gpuLoad = clamp100(s.value); break;
                    }
                }
            }
        }

        // ── PSU напряжения ───────────────────────────────────
        for (auto& s : sensors) {
            if (s.sensorType != L"Voltage") continue;
            std::wstring nl = toLower(s.name);
            if ((nl.find(L"+12v") != std::wstring::npos ||
                 nl.find(L"12v")  != std::wstring::npos) &&
                inRange(s.value, 10.0f, 14.0f)) {
                d.psu.v12 = s.value; d.psu.valid = true;
            } else if ((nl.find(L"+5v") != std::wstring::npos ||
                        nl == L"5v") &&
                       inRange(s.value, 4.0f, 6.0f)) {
                d.psu.v5 = s.value; d.psu.valid = true;
            } else if ((nl.find(L"3.3") != std::wstring::npos ||
                        nl.find(L"3v3") != std::wstring::npos) &&
                       inRange(s.value, 2.5f, 4.0f)) {
                d.psu.v33 = s.value; d.psu.valid = true;
            }
        }

        return d;
    }

    bool IsOhmRunning() {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32W pe = { sizeof(pe) };
        bool found = false;
        if (Process32FirstW(snap, &pe)) {
            do {
                std::wstring name(pe.szExeFile);
                std::transform(name.begin(), name.end(), name.begin(), ::towlower);
                if (name.find(L"openhardwaremonitor") != std::wstring::npos)
                    { found = true; break; }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return found;
    }

private:
    std::string m_ohmFullPath;
    std::string m_cpuName;
    std::string m_gpuName;

    struct Sensor {
        std::wstring name;
        std::wstring sensorType;
        std::wstring parent;   // /intelcpu/0, /nvidiagpu/0, …
        float        value = 0.0f;
    };

    // ── Запуск OHM ───────────────────────────────────────────
    void EnsureOhmRunning() {
        if (IsOhmRunning()) {
            std::cout << "[OHM] Already running\n";
            return;
        }
        DWORD attr = GetFileAttributesA(m_ohmFullPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            std::cout << "[OHM][ERROR] Not found: " << m_ohmFullPath << "\n";
            return;
        }

        // Рабочая папка = папка где лежит OHM.exe
        // КРИТИЧНО: OHM ищет свой WinRing0x64.sys рядом с собой.
        // Если WorkDir не совпадает — драйвер не грузится → все сенсоры = 0.
        std::string ohmDir = m_ohmFullPath.substr(0, m_ohmFullPath.find_last_of("\\/"));
        std::wstring wDir(ohmDir.begin(), ohmDir.end());
        std::wstring wPath(m_ohmFullPath.begin(), m_ohmFullPath.end());

        STARTUPINFOW si = {};
        si.cb          = sizeof(si);
        si.dwFlags     = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};

        // lpApplicationName без кавычек, lpCurrentDirectory = папка OHM
        BOOL ok = CreateProcessW(
            wPath.c_str(), NULL,
            NULL, NULL, FALSE,
            CREATE_NO_WINDOW,
            NULL,
            wDir.c_str(),   // ← рабочая папка
            &si, &pi);

        if (ok) {
            std::cout << "[OHM] Started PID: " << pi.dwProcessId
                      << " WorkDir: " << ohmDir << "\n";
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            std::cout << "[OHM][ERROR] CreateProcess failed: " << GetLastError() << "\n";
        }
    }

    // ── WMI connect ──────────────────────────────────────────
    bool WmiConnect(const wchar_t* ns,
                    IWbemLocator*& pLoc, IWbemServices*& pSvc) {
        pLoc = nullptr; pSvc = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WbemLocator, 0,
            CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
        if (FAILED(hr)) return false;
        hr = pLoc->ConnectServer(_bstr_t(ns),
            NULL, NULL, 0, NULL, 0, 0, &pSvc);
        if (FAILED(hr)) { pLoc->Release(); pLoc = nullptr; return false; }
        CoSetProxyBlanket(pSvc,
            RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
            NULL, EOAC_NONE);
        return true;
    }

    // ── Все сенсоры одним запросом ───────────────────────────
    std::vector<Sensor> QueryAllSensors() {
        std::vector<Sensor> result;
        IWbemLocator*  pLoc = nullptr;
        IWbemServices* pSvc = nullptr;
        if (!WmiConnect(L"ROOT\\OpenHardwareMonitor", pLoc, pSvc))
            return result;

        const wchar_t* query =
            L"SELECT Name, SensorType, Value, Parent FROM Sensor "
            L"WHERE SensorType='Temperature' "
            L"OR SensorType='Load' "
            L"OR SensorType='Voltage'";

        IEnumWbemClassObject* pEnum = nullptr;
        HRESULT hr = pSvc->ExecQuery(
            bstr_t("WQL"), bstr_t(query),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            NULL, &pEnum);

        if (SUCCEEDED(hr) && pEnum) {
            IWbemClassObject* pObj = nullptr; ULONG uRet = 0;
            while (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet) == S_OK) {
                Sensor s;
                ReadStr(pObj, L"Name",       s.name);
                ReadStr(pObj, L"SensorType", s.sensorType);
                ReadStr(pObj, L"Parent",     s.parent);
                VARIANT vv; VariantInit(&vv);
                if (SUCCEEDED(pObj->Get(L"Value", 0, &vv, 0, 0))) {
                    if      (vv.vt == VT_R4) s.value = vv.fltVal;
                    else if (vv.vt == VT_R8) s.value = (float)vv.dblVal;
                    else if (vv.vt == VT_I4) s.value = (float)vv.lVal;
                }
                VariantClear(&vv);
                result.push_back(s);
                pObj->Release();
            }
            pEnum->Release();
        }
        pSvc->Release();
        pLoc->Release();
        return result;
    }

    // ── Имена железа из Hardware таблицы ────────────────────
    void RefreshHardwareNames() {
        IWbemLocator*  pLoc = nullptr;
        IWbemServices* pSvc = nullptr;
        if (!WmiConnect(L"ROOT\\OpenHardwareMonitor", pLoc, pSvc)) return;

        IEnumWbemClassObject* pEnum = nullptr;
        HRESULT hr = pSvc->ExecQuery(
            bstr_t("WQL"),
            bstr_t("SELECT Name, HardwareType FROM Hardware"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            NULL, &pEnum);

        if (SUCCEEDED(hr) && pEnum) {
            IWbemClassObject* pObj = nullptr; ULONG uRet = 0;
            while (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet) == S_OK) {
                std::wstring name, type;
                ReadStr(pObj, L"Name",         name);
                ReadStr(pObj, L"HardwareType", type);
                std::wstring tl = toLower(type);
                if (m_cpuName.empty() &&
                    (tl == L"cpu" || tl == L"intelcpu" || tl == L"amdcpu"))
                    m_cpuName = wstrToStr(name);
                if (m_gpuName.empty() &&
                    (tl == L"gpunvidia" || tl == L"gpuati"))
                    m_gpuName = wstrToStr(name);
                pObj->Release();
            }
            pEnum->Release();
        }
        pSvc->Release();
        pLoc->Release();
    }

    // ── Дамп всех сенсоров в лог при старте ─────────────────
    void DumpSensors(const std::vector<Sensor>& sensors) {
        std::ofstream f("C:\\ohm_sensors.log");
        if (!f) return;
        f << "OHM Sensors dump (" << sensors.size() << " total)\n";
        f << "CPU: " << m_cpuName << "\n";
        f << "GPU: " << m_gpuName << "\n\n";
        for (auto& s : sensors) {
            f << "[" << wstrToStr(s.sensorType) << "] "
              << wstrToStr(s.name)
              << " = " << s.value
              << "  (parent: " << wstrToStr(s.parent) << ")\n";
        }
        std::cout << "[OHM] Sensor dump written: C:\\ohm_sensors.log\n";
    }

    // ── Helpers ──────────────────────────────────────────────
    static void ReadStr(IWbemClassObject* pObj,
                        const wchar_t* prop, std::wstring& out) {
        VARIANT v; VariantInit(&v);
        if (SUCCEEDED(pObj->Get(prop, 0, &v, 0, 0)) && v.vt == VT_BSTR)
            out = v.bstrVal;
        VariantClear(&v);
    }

    static std::wstring toLower(std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    }

    static bool inRange(float v, float lo, float hi) {
        return v > lo && v < hi;
    }

    static float clamp100(float v) {
        return v < 0.f ? 0.f : (v > 100.f ? 100.f : v);
    }

    static std::string wstrToStr(const std::wstring& w) {
        std::string s; s.reserve(w.size());
        for (wchar_t c : w) s += (c < 128) ? (char)c : '?';
        return s;
    }
};