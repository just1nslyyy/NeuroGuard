#pragma once
// ═══════════════════════════════════════════════════════════
//  CpuMonitor.hpp
//  CPU температура, нагрузка, имя через OHM + WinAPI
// ═══════════════════════════════════════════════════════════

#include "OhmProvider.hpp"
#include <string>
#include <vector>
#include <windows.h>
#include <algorithm>

using std::min;
using std::max;

struct CpuData {
    float       temp    = 0.0f;
    float       load    = 0.0f;
    std::string name;
    std::vector<int> coreLoads;
};

inline float getCpuTemp(const std::vector<OhmSensor>& sensors) {
    float coreSum = 0; int coreCnt = 0;

    for (auto& s : sensors) {
        if (s.sensorType != L"Temperature") continue;
        std::wstring nl = ohmToLower(s.name);
        if (nl == L"cpu package" && ohmInRange(s.value, 5, 120))
            return s.value;
    }
    for (auto& s : sensors) {
        if (s.sensorType != L"Temperature") continue;
        std::wstring nl = ohmToLower(s.name);
        if ((nl.find(L"ccd")  != std::wstring::npos ||
             nl.find(L"tdie") != std::wstring::npos ||
             nl.find(L"tctl") != std::wstring::npos) &&
            ohmInRange(s.value, 5, 120))
            return s.value;
    }
    for (auto& s : sensors) {
        if (s.sensorType != L"Temperature") continue;
        std::wstring nl = ohmToLower(s.name);
        if ((nl.find(L"cpu core") != std::wstring::npos ||
             nl.find(L"core #")   != std::wstring::npos) &&
            ohmInRange(s.value, 5, 120)) {
            coreSum += s.value; coreCnt++;
        }
    }
    if (coreCnt > 0) return coreSum / coreCnt;

    for (auto& s : sensors) {
        if (s.sensorType != L"Temperature") continue;
        std::wstring pl = ohmToLower(s.parent);
        if (pl.find(L"cpu") != std::wstring::npos &&
            ohmInRange(s.value, 5, 120))
            return s.value;
    }
    return 0.0f;
}

inline float getCpuLoad(const std::vector<OhmSensor>& sensors) {
    for (auto& s : sensors) {
        if (s.sensorType != L"Load") continue;
        std::wstring nl = ohmToLower(s.name);
        if (nl.find(L"cpu total") != std::wstring::npos)
            return ohmClamp100(s.value);
    }
    float sum = 0; int cnt = 0;
    for (auto& s : sensors) {
        if (s.sensorType != L"Load") continue;
        std::wstring nl = ohmToLower(s.name);
        if (nl.find(L"cpu core") != std::wstring::npos)
            { sum += s.value; cnt++; }
    }
    return cnt > 0 ? ohmClamp100(sum / cnt) : 0.0f;
}

inline std::vector<int> getCoreLoads() {
    typedef LONG (WINAPI*NtQSI_t)(UINT, PVOID, ULONG, PULONG);
    static NtQSI_t NtQSI = (NtQSI_t)GetProcAddress(
        GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation");

    std::vector<int> loads;
    if (!NtQSI) return loads;

    SYSTEM_INFO si; GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors;

    struct PerfInfo {
        LARGE_INTEGER IdleTime, KernelTime, UserTime,
                      DpcTime, InterruptTime;
        ULONG InterruptCount;
    };

    static std::vector<PerfInfo> prev(n);
    std::vector<PerfInfo> cur(n);

    if (NtQSI(8, cur.data(), sizeof(PerfInfo)*n, NULL) == 0) {
        for (int i = 0; i < n; i++) {
            ULONGLONG idle   = cur[i].IdleTime.QuadPart
                             - prev[i].IdleTime.QuadPart;
            ULONGLONG kernel = cur[i].KernelTime.QuadPart
                             - prev[i].KernelTime.QuadPart;
            ULONGLONG user   = cur[i].UserTime.QuadPart
                             - prev[i].UserTime.QuadPart;
            ULONGLONG total  = kernel + user;
            int load = (total > 0)
                ? (int)((total - idle) * 100ULL / total) : 0;
            loads.push_back(max(0, min(100, load)));
        }
        prev = cur;
    }
    return loads;
}