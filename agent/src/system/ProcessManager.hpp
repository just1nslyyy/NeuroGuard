#pragma once
#include "../pch.hpp"
// ═══════════════════════════════════════════════════════════
//  ProcessManager.hpp
//  Топ процессы по CPU/RAM, kill процесса
// ═══════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <psapi.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "psapi.lib")

struct ProcessInfo {
    std::string name;
    DWORD       pid        = 0;
    float       memoryMb   = 0.0f;
    float       cpuUsage   = 0.0f;
};

static unsigned long long FileTimeToULL(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

inline std::vector<ProcessInfo> getTopProcesses(int limit = 5) {
    DWORD pids[2048], cb;
    if (!EnumProcesses(pids, sizeof(pids), &cb)) return {};
    DWORD cnt = cb / sizeof(DWORD);

    SYSTEM_INFO si; GetSystemInfo(&si);
    DWORD numCpu = max((DWORD)1, si.dwNumberOfProcessors);

    struct Snap {
        DWORD pid;
        unsigned long long kt, ut;
        float mb;
        std::wstring name;
    };

    std::vector<Snap> snaps;
    snaps.reserve(cnt);

    for (DWORD i = 0; i < cnt; i++) {
        HANDLE h = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pids[i]);
        if (!h) continue;

        FILETIME ct, et, kt, ut;
        if (!GetProcessTimes(h, &ct, &et, &kt, &ut)) {
            CloseHandle(h); continue;
        }

        TCHAR nm[MAX_PATH] = TEXT("<unknown>");
        HMODULE hm; DWORD cb2;
        if (EnumProcessModules(h, &hm, sizeof(hm), &cb2))
            GetModuleBaseName(h, hm, nm, MAX_PATH);

        PROCESS_MEMORY_COUNTERS pmc;
        float mb = GetProcessMemoryInfo(h, &pmc, sizeof(pmc))
            ? (float)pmc.WorkingSetSize / (1024.f * 1024.f) : 0;

        snaps.push_back({pids[i],
            FileTimeToULL(kt), FileTimeToULL(ut),
            mb, std::wstring(nm)});
        CloseHandle(h);
    }

    Sleep(50);

    std::vector<ProcessInfo> result;
    for (auto& s : snaps) {
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, s.pid);
        if (!h) continue;

        FILETIME ct, et, kt, ut;
        float cpu = 0;
        if (GetProcessTimes(h, &ct, &et, &kt, &ut)) {
            unsigned long long delta =
                (FileTimeToULL(kt) + FileTimeToULL(ut)) - (s.kt + s.ut);
            cpu = min(100.f, max(0.f,
                100.f * (float)delta / (500000.f * (float)numCpu)));
        }
        CloseHandle(h);

        ProcessInfo pi;
        pi.pid      = s.pid;
        pi.memoryMb = s.mb;
        pi.cpuUsage = cpu;
        pi.name.assign(s.name.begin(), s.name.end());
        result.push_back(pi);
    }

    std::sort(result.begin(), result.end(),
        [](auto& a, auto& b){ return a.cpuUsage > b.cpuUsage; });
    if ((int)result.size() > limit) result.resize(limit);
    return result;
}

inline void killProcess(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (h) { TerminateProcess(h, 1); CloseHandle(h); }
}