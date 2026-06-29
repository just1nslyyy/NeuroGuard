#pragma once
#include "../pch.hpp"
// ═══════════════════════════════════════════════════════════
//  PowerManager.hpp
//  Питание, батарея, uptime, sleep/shutdown
// ═══════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <powrprof.h>
#include <string>

#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "advapi32.lib")

struct PowerData {
    std::string source         = "AC";
    int         batteryPercent = 100;
    long        uptime         = 0;
};

inline PowerData getPowerData() {
    SYSTEM_POWER_STATUS sps = {};
    PowerData d;
    if (GetSystemPowerStatus(&sps)) {
        if      (sps.ACLineStatus == 1) d.source = "AC";
        else if (sps.ACLineStatus == 0) d.source = "Battery";
        else                            d.source = "Unknown";
        d.batteryPercent = (sps.BatteryLifePercent != 255)
            ? (int)sps.BatteryLifePercent : 100;
    }
    d.uptime = (long)(GetTickCount64() / 1000ULL);
    return d;
}

inline void doSleep() {
    SetSuspendState(FALSE, TRUE, FALSE);
}

inline void doShutdown() {
    HANDLE hTok;
    TOKEN_PRIVILEGES tkp;
    if (OpenProcessToken(GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hTok)) {
        LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME,
            &tkp.Privileges[0].Luid);
        tkp.PrivilegeCount = 1;
        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hTok, FALSE, &tkp, 0, NULL, NULL);
        CloseHandle(hTok);
    }
    InitiateSystemShutdownExW(NULL, (LPWSTR)L"NeuroGuard",
        30, TRUE, FALSE, SHTDN_REASON_MAJOR_OTHER);
}

inline void cancelShutdown() {
    AbortSystemShutdownW(NULL);
}