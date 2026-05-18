#ifndef NATIVE_COLLECTOR_H
#define NATIVE_COLLECTOR_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <pdh.h>

#pragma comment(lib, "pdh.lib")

struct RamData {
    float totalGb  = 0.0f;
    float usedGb   = 0.0f;
    int   percent  = 0;
};

struct DiskData {
    std::string name;
    float totalGb  = 0.0f;
    float usedGb   = 0.0f;
    int   percent  = 0;
};

struct PowerData {
    std::string source        = "AC";
    int         batteryPercent = 100;
    long        uptime         = 0;
};

// ── FIX: wchar_t → char через WideCharToMultiByte (без warning C4244) ──
inline std::string WcharToUtf8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (sz <= 0) return "";
    std::string out(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &out[0], sz, nullptr, nullptr);
    return out;
}

inline float getCpuLoadNative() {
    static PDH_HQUERY   cpuQuery  = NULL;
    static PDH_HCOUNTER cpuTotal  = NULL;
    static bool         initialized = false;

    if (!initialized) {
        if (PdhOpenQueryW(NULL, 0, &cpuQuery) == ERROR_SUCCESS) {
            PdhAddEnglishCounterW(cpuQuery,
                L"\\Processor(_Total)\\% Processor Time", 0, &cpuTotal);
            PdhCollectQueryData(cpuQuery);
            initialized = true;
        }
        return 0.0f;
    }

    PDH_FMT_COUNTERVALUE cv;
    if (PdhCollectQueryData(cpuQuery) == ERROR_SUCCESS &&
        PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &cv) == ERROR_SUCCESS) {
        float v = (float)cv.doubleValue;
        return (v < 0.f) ? 0.f : (v > 100.f ? 100.f : v);
    }
    return 0.0f;
}

inline RamData getExtendedRamNative() {
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    RamData d;
    if (GlobalMemoryStatusEx(&mem)) {
        d.totalGb  = (float)mem.ullTotalPhys / (1024.f * 1024.f * 1024.f);
        float free = (float)mem.ullAvailPhys  / (1024.f * 1024.f * 1024.f);
        d.usedGb   = d.totalGb - free;
        d.percent  = (int)mem.dwMemoryLoad;
    }
    return d;
}

inline std::vector<DiskData> getAllDisksNative() {
    std::vector<DiskData> disks;
    wchar_t buf[MAX_PATH] = {};
    DWORD r = GetLogicalDriveStringsW(MAX_PATH, buf);
    if (r == 0 || r > MAX_PATH) return disks;

    wchar_t* drive = buf;
    while (*drive) {
        ULARGE_INTEGER free64, total64, totalFree64;
        if (GetDiskFreeSpaceExW(drive, &free64, &total64, &totalFree64)) {
            DiskData d;

            // FIX: правильная конвертация wchar_t → string
            std::wstring wDrive(drive);
            // Убираем trailing backslash: L"C:\\" → L"C:"
            while (!wDrive.empty() &&
                   (wDrive.back() == L'\\' || wDrive.back() == L'/'))
                wDrive.pop_back();
            d.name = WcharToUtf8(wDrive.c_str());

            d.totalGb = (float)total64.QuadPart / (1024.f * 1024.f * 1024.f);
            float freeGb = (float)free64.QuadPart / (1024.f * 1024.f * 1024.f);
            d.usedGb  = d.totalGb - freeGb;
            d.percent = (d.totalGb > 0)
                ? (int)(100.f * (1.f - freeGb / d.totalGb))
                : 0;
            disks.push_back(d);
        }
        drive += wcslen(drive) + 1;
    }
    return disks;
}

inline PowerData getPowerStatusNative() {
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

#endif // NATIVE_COLLECTOR_H