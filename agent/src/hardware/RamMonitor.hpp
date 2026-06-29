#pragma once
#include "../pch.hpp"
// ═══════════════════════════════════════════════════════════
//  RamMonitor.hpp
//  Сбор данных об оперативной памяти через WinAPI
// ═══════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

struct RamData {
    float totalGb = 0.0f;
    float usedGb  = 0.0f;
    int   percent = 0;
};

inline RamData getRamData() {
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    RamData d;
    if (GlobalMemoryStatusEx(&mem)) {
        d.totalGb = (float)mem.ullTotalPhys / (1024.f * 1024.f * 1024.f);
        float freeGb = (float)mem.ullAvailPhys / (1024.f * 1024.f * 1024.f);
        d.usedGb  = d.totalGb - freeGb;
        d.percent = (int)mem.dwMemoryLoad;
    }
    return d;
}