#pragma once
#include "../pch.hpp"
// ═══════════════════════════════════════════════════════════
//  DeviceIdentity.hpp
//  HWID устройства, автозапуск, реестр
// ═══════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <iphlpapi.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "iphlpapi.lib")

inline std::string getHWID() {
    DWORD vol = 0;
    GetVolumeInformationW(L"C:\\", NULL, 0, &vol, NULL, NULL, NULL, 0);

    ULONG bufLen = sizeof(IP_ADAPTER_INFO) * 16;
    std::vector<BYTE> buf(bufLen);
    DWORD ret = GetAdaptersInfo((IP_ADAPTER_INFO*)buf.data(), &bufLen);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        buf.resize(bufLen);
        ret = GetAdaptersInfo((IP_ADAPTER_INFO*)buf.data(), &bufLen);
    }

    std::string mac = "000000000000";
    if (ret == NO_ERROR) {
        auto* a = (IP_ADAPTER_INFO*)buf.data();
        while (a) {
            std::string desc(a->Description);
            bool virt = false;
            for (const auto& kw : {
                "VMware","VirtualBox","Hyper-V",
                "TAP","Loopback","Pseudo"})
                if (desc.find(kw) != std::string::npos) {
                    virt = true; break;
                }
            if (!virt && a->AddressLength == 6) {
                char m[32];
                snprintf(m, sizeof(m), "%02X%02X%02X%02X%02X%02X",
                    a->Address[0], a->Address[1], a->Address[2],
                    a->Address[3], a->Address[4], a->Address[5]);
                mac = m; break;
            }
            a = a->Next;
        }
    }

    std::ostringstream oss;
    oss << std::hex << std::uppercase << vol << "-" << mac;
    return oss.str();
}

inline bool isAutorunAsked() {
    HKEY hk;
    DWORD v = 0, s = sizeof(DWORD);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\NeuroGuard", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        RegQueryValueExW(hk, L"AutorunAsked",
            NULL, NULL, (LPBYTE)&v, &s);
        RegCloseKey(hk);
    }
    return v == 1;
}

inline void saveAutorunAsked() {
    HKEY hk; DWORD v = 1;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\NeuroGuard", 0, NULL, 0,
        KEY_WRITE, NULL, &hk, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hk, L"AutorunAsked", 0, REG_DWORD,
            (const BYTE*)&v, sizeof(DWORD));
        RegCloseKey(hk);
    }
}

inline void setAutorun(bool enable) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        if (enable) {
            WCHAR p[MAX_PATH];
            GetModuleFileNameW(NULL, p, MAX_PATH);
            std::wstring q = std::wstring(L"\"") + p + L"\" --background";
            RegSetValueExW(hk, L"NeuroGuard", 0, REG_SZ,
                (const BYTE*)q.c_str(),
                (DWORD)((q.size() + 1) * sizeof(WCHAR)));
        } else {
            RegDeleteValueW(hk, L"NeuroGuard");
        }
        RegCloseKey(hk);
    }
}

inline bool isRunningAsAdmin() {
    BOOL r = FALSE;
    HANDLE t = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) {
        TOKEN_ELEVATION e; DWORD s = sizeof(e);
        if (GetTokenInformation(t, TokenElevation, &e, sizeof(e), &s))
            r = e.TokenIsElevated;
        CloseHandle(t);
    }
    return r;
}

inline void restartAsAdmin() {
    WCHAR p[MAX_PATH];
    if (!GetModuleFileNameW(NULL, p, MAX_PATH)) return;
    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.lpVerb = L"runas";
    sei.lpFile = p;
    sei.nShow  = SW_NORMAL;
    if (!ShellExecuteExW(&sei)) exit(0);
}