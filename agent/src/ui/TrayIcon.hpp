#pragma once
#include "../pch.hpp"
// ═══════════════════════════════════════════════════════════
//  TrayIcon.hpp
//  Иконка в трее, уведомления, обработка сообщений
// ═══════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <shellapi.h>
#include <string>

#pragma comment(lib, "shell32.lib")

#define ID_TRAY_EXIT       1001
#define WM_TRAYICON        (WM_USER + 1)
#define WM_SCREENSHOT_DONE (WM_USER + 2)

inline NOTIFYICONDATA g_nid     = {};
inline HWND           g_hTrayWnd = NULL;

inline LRESULT CALLBACK TrayWindowProc(
    HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_TRAYICON && lParam == WM_RBUTTONUP) {
        POINT pt; GetCursorPos(&pt);
        HMENU hm = CreatePopupMenu();
        AppendMenuW(hm, MF_STRING, ID_TRAY_EXIT, L"Выход");
        SetForegroundWindow(hwnd);
        TrackPopupMenu(hm, TPM_LEFTALIGN | TPM_RIGHTBUTTON,
            pt.x, pt.y, 0, hwnd, NULL);
        PostMessage(hwnd, WM_NULL, 0, 0);
        DestroyMenu(hm);
        return 0;
    }
    if (uMsg == WM_COMMAND && LOWORD(wParam) == ID_TRAY_EXIT) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        exit(0);
    }
    if (uMsg == WM_SCREENSHOT_DONE) {
        // уведомление придёт из TrayIcon::showNotification
        return 0;
    }
    if (uMsg == WM_QUERYENDSESSION || uMsg == WM_ENDSESSION) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        return TRUE;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

inline bool initTrayIcon() {
    WNDCLASSEXW wc  = {sizeof(wc)};
    wc.lpfnWndProc  = TrayWindowProc;
    wc.hInstance    = GetModuleHandle(NULL);
    wc.lpszClassName = L"NeuroGuardTrayClass";
    RegisterClassExW(&wc);

    g_hTrayWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0,
        0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (!g_hTrayWnd) return false;

    g_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd             = g_hTrayWnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"NeuroGuard Agent");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    return true;
}

inline void showTrayNotification(const std::wstring& title,
                                  const std::wstring& msg,
                                  DWORD flags = NIIF_INFO) {
    g_nid.uFlags |= NIF_INFO;
    wcscpy_s(g_nid.szInfoTitle, title.c_str());
    wcscpy_s(g_nid.szInfo,      msg.c_str());
    g_nid.dwInfoFlags = flags;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags &= ~NIF_INFO;
}

inline void processTrayMessages() {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

inline void destroyTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}