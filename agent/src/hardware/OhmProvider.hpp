#pragma once
#include "../pch.hpp"
#define NOMINMAX
// ═══════════════════════════════════════════════════════════
//  OhmProvider.hpp
//  Общий WMI провайдер для OpenHardwareMonitor
// ═══════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <WbemIdl.h>
#include <comdef.h>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ws2_32.lib")

struct OhmSensor {
    std::wstring name;
    std::wstring sensorType;
    std::wstring parent;
    float        value = 0.0f;
};

inline std::wstring ohmToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

inline bool ohmInRange(float v, float lo, float hi) {
    return v > lo && v < hi;
}

inline float ohmClamp100(float v) {
    return v < 0.f ? 0.f : (v > 100.f ? 100.f : v);
}

inline std::string ohmWstrToStr(const std::wstring& w) {
    std::string s; s.reserve(w.size());
    for (wchar_t c : w) s += (c < 128) ? (char)c : '?';
    return s;
}

inline void ohmReadStr(IWbemClassObject* pObj,
                        const wchar_t* prop,
                        std::wstring& out) {
    VARIANT v; VariantInit(&v);
    if (SUCCEEDED(pObj->Get(prop, 0, &v, 0, 0)) && v.vt == VT_BSTR)
        out = v.bstrVal;
    VariantClear(&v);
}

inline bool ohmWmiConnect(const wchar_t* ns,
                           IWbemLocator*& pLoc,
                           IWbemServices*& pSvc) {
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

inline std::vector<OhmSensor> ohmQueryAllSensors() {
    std::vector<OhmSensor> result;
    IWbemLocator*  pLoc = nullptr;
    IWbemServices* pSvc = nullptr;
    if (!ohmWmiConnect(L"ROOT\\OpenHardwareMonitor", pLoc, pSvc))
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
            OhmSensor s;
            ohmReadStr(pObj, L"Name",       s.name);
            ohmReadStr(pObj, L"SensorType", s.sensorType);
            ohmReadStr(pObj, L"Parent",     s.parent);
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

inline bool ohmIsRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = {sizeof(pe)};
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring name(pe.szExeFile);
            std::transform(name.begin(), name.end(),
                name.begin(), ::towlower);
            if (name.find(L"openhardwaremonitor") != std::wstring::npos)
                { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

inline void ohmEnsureRunning(const std::string& ohmFullPath) {
    if (ohmIsRunning()) {
        std::cout << "[OHM] Already running\n";
        return;
    }
    DWORD attr = GetFileAttributesA(ohmFullPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        std::cout << "[OHM][ERROR] Not found: " << ohmFullPath << "\n";
        return;
    }
    std::string ohmDir = ohmFullPath.substr(
        0, ohmFullPath.find_last_of("\\/"));
    std::wstring wDir(ohmDir.begin(), ohmDir.end());
    std::wstring wPath(ohmFullPath.begin(), ohmFullPath.end());

    STARTUPINFOW si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    BOOL ok = CreateProcessW(wPath.c_str(), NULL,
        NULL, NULL, FALSE, CREATE_NO_WINDOW,
        NULL, wDir.c_str(), &si, &pi);

    if (ok) {
        std::cout << "[OHM] Started PID: " << pi.dwProcessId << "\n";
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        std::cout << "[OHM][ERROR] CreateProcess failed: "
                  << GetLastError() << "\n";
    }
}