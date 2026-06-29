// ═══════════════════════════════════════════════════════════
//  main.cpp
//  NeuroGuard Agent — точка входа
// ═══════════════════════════════════════════════════════════

#include "pch.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>

#include "hardware/CpuMonitor.hpp"
#include "hardware/GpuMonitor.hpp"
#include "hardware/RamMonitor.hpp"
#include "hardware/DiskMonitor.hpp"
#include "hardware/NetworkMonitor.hpp"
#include "system/PowerManager.hpp"
#include "system/ProcessManager.hpp"
#include "system/ScreenCapture.hpp"
#include "system/DeviceIdentity.hpp"
#include "network/TelemetrySender.hpp"
#include "network/AuthManager.hpp"
#include "security/DefenderGuard.hpp"
#include "ui/TrayIcon.hpp"

const std::string SERVER_URL = "http://IP";

// ── Температурная история ────────────────────────────────
struct TempHistory {
    std::vector<float> cpuTemps, gpuTemps;
};

TempHistory g_tempHistory;
std::mutex  g_tempMutex;
const int   MAX_HISTORY = 3600;

void recordTemperatures(float cpuTemp, float gpuTemp) {
    std::lock_guard<std::mutex> lk(g_tempMutex);
    auto push = [](std::vector<float>& v, float val) {
        if (val > 0) {
            v.push_back(val);
            if ((int)v.size() > MAX_HISTORY) v.erase(v.begin());
        }
    };
    push(g_tempHistory.cpuTemps, cpuTemp);
    push(g_tempHistory.gpuTemps, gpuTemp);
}

float getAvgTemp(const std::vector<float>& v) {
    std::lock_guard<std::mutex> lk(g_tempMutex);
    if (v.empty()) return 0.0f;
    float s = 0; for (auto t : v) s += t;
    return s / (float)v.size();
}

float getMaxTemp(const std::vector<float>& v) {
    std::lock_guard<std::mutex> lk(g_tempMutex);
    return v.empty() ? 0.0f : *std::max_element(v.begin(), v.end());
}

// ── Фоновый режим ────────────────────────────────────────
bool   g_isBackgroundMode  = false;
bool   g_startedFromClose  = false;
HANDLE g_backgroundMutex   = NULL;

bool launchBackgroundInstance(bool fromClose) {
    HANDLE hCheck = OpenMutexW(SYNCHRONIZE, FALSE,
        L"Global\\NeuroGuardAgentMutex");
    if (hCheck) { CloseHandle(hCheck); return true; }

    WCHAR p[MAX_PATH] = {};
    if (!GetModuleFileNameW(NULL, p, MAX_PATH)) return false;

    std::wstring cmd = std::wstring(L"\"") + p + L"\" --background";
    if (fromClose) cmd += L" --from-close";

    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(NULL, (LPWSTR)cmd.c_str(),
        NULL, NULL, FALSE,
        CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,
        NULL, NULL, &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void ensureConsole() {
    if (!GetConsoleWindow()) AllocConsole();
    FILE* d = nullptr;
    freopen_s(&d, "CONIN$",  "r", stdin);
    freopen_s(&d, "CONOUT$", "w", stdout);
    freopen_s(&d, "CONOUT$", "w", stderr);
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
}

// ── main ─────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--background") g_isBackgroundMode = true;
        if (a == "--from-close") g_startedFromClose = true;
    }

    if (!isRunningAsAdmin()) { restartAsAdmin(); return 0; }

    if (g_isBackgroundMode) {
        g_backgroundMutex = CreateMutexW(NULL, TRUE,
            L"Global\\NeuroGuardAgentMutex");
        if (g_backgroundMutex &&
            GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(g_backgroundMutex); return 0;
        }
    }

    ensureDefenderExclusion();

    if (!g_isBackgroundMode) {
        launchBackgroundInstance(false);
        ensureConsole();
        std::string hwid = getHWID();
        if (!isAutorunAsked()) {
            std::cout << "[?] Добавить в автозапуск? (y/n): ";
            char c; std::cin >> c;
            if (c == 'y' || c == 'Y') {
                setAutorun(true);
                std::cout << "[+] Добавлено\n";
            } else {
                std::cout << "[-] Пропущено\n";
            }
            saveAutorunAsked();
        }
        requestAuthCode(hwid, SERVER_URL);
        std::cout << "[*] Агент запущен в фоне.\n";
        while (true)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // ── Инициализация ────────────────────────────────────
    initTrayIcon();
    if (g_startedFromClose)
        showTrayNotification(L"NeuroGuard", L"Агент работает в фоне");

    HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    // OHM
    std::string ohmPath;
    {
        char exeDir[MAX_PATH];
        GetModuleFileNameA(NULL, exeDir, MAX_PATH);
        std::string dir(exeDir);
        dir = dir.substr(0, dir.find_last_of("\\/"));
        ohmPath = dir + "\\OpenHardwareMonitor.exe";
    }
    ohmEnsureRunning(ohmPath);
    Sleep(3000);

    std::string hwid = getHWID();
    std::string rank = "free";
    bool shouldStop  = false;

    NetState    netState;
    std::string cpuName, gpuName;
    double totalUptime = 0.0;
    auto   startTime   = std::chrono::steady_clock::now();
    int    watchdogTick = 0;

    std::cout << "[*] HWID: " << hwid << "\n\n";

    // ── Главный цикл ─────────────────────────────────────
    while (!shouldStop) {
        processTrayMessages();

        auto   now = std::chrono::steady_clock::now();
        double dt  = std::chrono::duration<double>(now - startTime).count();
        startTime  = now;
        if (dt >= 0.1 && dt < 60.0) totalUptime += dt;
        double kwh = (550.0 * totalUptime / 3600.0) / 1000.0;

        if (++watchdogTick >= 15) {
            if (!ohmIsRunning()) {
                std::cout << "[OHM] Died, restarting...\n";
                ohmEnsureRunning(ohmPath);
                Sleep(4000);
            }
            watchdogTick = 0;
        }

        try {
            auto sensors = ohmQueryAllSensors();
            if (cpuName.empty() || gpuName.empty())
                getHardwareNames(cpuName, gpuName);

            OhmData ohm;
            ohm.cpuTemp = getCpuTemp(sensors);
            ohm.cpuLoad = getCpuLoad(sensors);
            ohm.gpuTemp = getGpuTemp(sensors);
            ohm.gpuLoad = getGpuLoad(sensors);
            ohm.cpuName = cpuName;
            ohm.gpuName = gpuName;

            recordTemperatures(ohm.cpuTemp, ohm.gpuTemp);

            TelemetryPayload payload;
            payload.hwid          = hwid;
            payload.ohm           = ohm;
            payload.ram           = getRamData();
            payload.disks         = getDiskData();
            payload.power         = getPowerData();
            payload.net           = getNetSpeed(netState);
            payload.processes     = getTopProcesses();
            payload.coreLoads     = getCoreLoads();
            payload.sessionUptime = totalUptime;
            payload.estimatedKwh  = kwh;
            payload.cpuTempAvg    = getAvgTemp(g_tempHistory.cpuTemps);
            payload.cpuTempMax    = getMaxTemp(g_tempHistory.cpuTemps);
            payload.gpuTempAvg    = getAvgTemp(g_tempHistory.gpuTemps);
            payload.gpuTempMax    = getMaxTemp(g_tempHistory.gpuTemps);

            auto resp = sendTelemetry(payload, SERVER_URL, shouldStop);

            if (resp.valid) {
                rank = resp.rank;
                std::string cmd = resp.command;

                if (cmd == "screenshot" && rank == "overseer") {
                    std::thread([hwid](){
                        uploadScreenshot(hwid, takeScreenshot(), SERVER_URL);
                        if (g_hTrayWnd)
                            PostMessage(g_hTrayWnd, WM_SCREENSHOT_DONE, 0, 0);
                    }).detach();
                } else if (cmd == "sleep") {
                    doSleep();
                } else if (cmd == "shutdown") {
                    doShutdown();
                } else if (cmd == "cancel_shutdown") {
                    cancelShutdown();
                } else if (cmd.find("kill_") == 0) {
                    try {
                        DWORD pid = (DWORD)std::stoi(cmd.substr(5));
                        killProcess(pid);
                    } catch (...) {}
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[!] Loop error: " << e.what() << "\n";
        }

        for (int i = 0; i < 30 && !shouldStop; i++) {
            processTrayMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    destroyTrayIcon();
    if (SUCCEEDED(hrCo)) CoUninitialize();
    if (g_backgroundMutex) CloseHandle(g_backgroundMutex);
    return 0;
}