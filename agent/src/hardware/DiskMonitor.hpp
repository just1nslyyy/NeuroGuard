#pragma once
#include "../pch.hpp"
// ═══════════════════════════════════════════════════════════
//  DiskMonitor.hpp
//  Сбор данных о дисках через WinAPI + WMI (модель диска)
// ═══════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <WbemIdl.h>
#include <comdef.h>
#include <string>
#include <vector>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

struct DiskData {
    std::string name;
    float totalGb = 0.0f;
    float usedGb  = 0.0f;
    int   percent = 0;
};

inline std::string WcharToUtf8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (sz <= 0) return "";
    std::string out(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &out[0], sz, nullptr, nullptr);
    return out;
}

inline std::vector<DiskData> getDiskData() {
    std::vector<DiskData> disks;
    wchar_t buf[MAX_PATH] = {};
    DWORD r = GetLogicalDriveStringsW(MAX_PATH, buf);
    if (r == 0 || r > MAX_PATH) return disks;

    wchar_t* drive = buf;
    while (*drive) {
        ULARGE_INTEGER free64, total64, totalFree64;
        if (GetDiskFreeSpaceExW(drive, &free64, &total64, &totalFree64)) {
            DiskData d;
            std::wstring wDrive(drive);
            while (!wDrive.empty() &&
                   (wDrive.back() == L'\\' || wDrive.back() == L'/'))
                wDrive.pop_back();
            d.name    = WcharToUtf8(wDrive.c_str());
            d.totalGb = (float)total64.QuadPart / (1024.f * 1024.f * 1024.f);
            float freeGb = (float)free64.QuadPart / (1024.f * 1024.f * 1024.f);
            d.usedGb  = d.totalGb - freeGb;
            d.percent = (d.totalGb > 0)
                ? (int)(100.f * (1.f - freeGb / d.totalGb)) : 0;
            disks.push_back(d);
        }
        drive += wcslen(drive) + 1;
    }
    return disks;
}

inline std::string getDiskModel() {
    static std::string cached;
    if (!cached.empty()) return cached;

    std::string result;
    IWbemLocator*  pLoc = nullptr;
    IWbemServices* pSvc = nullptr;

    if (FAILED(CoCreateInstance(CLSID_WbemLocator, 0,
        CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc)))
        return "N/A";

    if (FAILED(pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"),
        nullptr, nullptr, nullptr, 0L, nullptr, 0, &pSvc))) {
        pLoc->Release(); return "N/A";
    }

    CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
        nullptr, RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    IEnumWbemClassObject* pEnum = nullptr;
    if (SUCCEEDED(pSvc->ExecQuery(bstr_t("WQL"),
        bstr_t("SELECT Model FROM Win32_DiskDrive"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &pEnum))) {
        IWbemClassObject* pObj = nullptr; ULONG ret = 0;
        while (pEnum && pEnum->Next(WBEM_INFINITE, 1, &pObj, &ret) == S_OK) {
            VARIANT v; VariantInit(&v);
            if (SUCCEEDED(pObj->Get(L"Model", 0, &v, 0, 0)) && v.vt == VT_BSTR) {
                if (!result.empty()) result += ", ";
                result += std::string(_bstr_t(v.bstrVal));
                VariantClear(&v);
            }
            pObj->Release();
        }
        if (pEnum) pEnum->Release();
    }
    pSvc->Release();
    pLoc->Release();
    cached = result.empty() ? "N/A" : result;
    return cached;
}