#pragma once
// ═══════════════════════════════════════════════════════════
//  GpuMonitor.hpp
//  GPU температура, нагрузка, имя через OHM WMI
// ═══════════════════════════════════════════════════════════

#include "OhmProvider.hpp"
#include <string>
#include <vector>

struct GpuData {
    float       temp = 0.0f;
    float       load = 0.0f;
    std::string name;
};

inline float getGpuTemp(const std::vector<OhmSensor>& sensors) {
    for (auto& s : sensors) {
        if (s.sensorType != L"Temperature") continue;
        std::wstring nl = ohmToLower(s.name);
        if (nl == L"gpu core" && ohmInRange(s.value, 5, 120))
            return s.value;
    }
    for (auto& s : sensors) {
        if (s.sensorType != L"Temperature") continue;
        std::wstring nl = ohmToLower(s.name);
        if (nl.find(L"gpu") != std::wstring::npos &&
            ohmInRange(s.value, 5, 120))
            return s.value;
    }
    return 0.0f;
}

inline float getGpuLoad(const std::vector<OhmSensor>& sensors) {
    for (auto& s : sensors) {
        if (s.sensorType != L"Load") continue;
        std::wstring nl = ohmToLower(s.name);
        if (nl == L"gpu core")
            return ohmClamp100(s.value);
    }
    for (auto& s : sensors) {
        if (s.sensorType != L"Load") continue;
        std::wstring nl = ohmToLower(s.name);
        if (nl.find(L"gpu") != std::wstring::npos)
            return ohmClamp100(s.value);
    }
    return 0.0f;
}

inline void getHardwareNames(std::string& cpuName,
                              std::string& gpuName) {
    IWbemLocator*  pLoc = nullptr;
    IWbemServices* pSvc = nullptr;
    if (!ohmWmiConnect(L"ROOT\\OpenHardwareMonitor", pLoc, pSvc))
        return;

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
            ohmReadStr(pObj, L"Name",         name);
            ohmReadStr(pObj, L"HardwareType", type);
            std::wstring tl = ohmToLower(type);
            if (cpuName.empty() &&
                (tl == L"cpu" || tl == L"intelcpu" || tl == L"amdcpu"))
                cpuName = ohmWstrToStr(name);
            if (gpuName.empty() &&
                (tl == L"gpunvidia" || tl == L"gpuati"))
                gpuName = ohmWstrToStr(name);
            pObj->Release();
        }
        pEnum->Release();
    }
    pSvc->Release();
    pLoc->Release();
}