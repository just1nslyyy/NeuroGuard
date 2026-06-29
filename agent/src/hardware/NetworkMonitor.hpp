#pragma once
#include "../pch.hpp"
// ═══════════════════════════════════════════════════════════
//  NetworkMonitor.hpp
//  Скорость сети (download/upload) через GetIfTable2
// ═══════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <chrono>
#include <utility>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

struct NetSpeed {
    double downloadMbps = 0.0;
    double uploadMbps   = 0.0;
};

struct NetState {
    ULONG64 lastIn  = 0;
    ULONG64 lastOut = 0;
    std::chrono::steady_clock::time_point lastTime = std::chrono::steady_clock::now();
};

inline NetSpeed getNetSpeed(NetState& state) {
    MIB_IF_TABLE2* pTable = nullptr;
    if (GetIfTable2(&pTable) != NO_ERROR) return {};

    ULONG64 curIn = 0, curOut = 0;
    for (ULONG i = 0; i < pTable->NumEntries; i++) {
        auto& e = pTable->Table[i];
        if (e.Type != IF_TYPE_SOFTWARE_LOOPBACK &&
            e.OperStatus == IfOperStatusUp) {
            curIn  += e.InOctets;
            curOut += e.OutOctets;
        }
    }
    FreeMibTable(pTable);

    auto   now = std::chrono::steady_clock::now();
    double dt  = std::chrono::duration<double>(now - state.lastTime).count();

    NetSpeed result;
    if (state.lastIn > 0 && dt > 0.1 &&
        curIn  >= state.lastIn &&
        curOut >= state.lastOut) {
        result.downloadMbps = ((curIn  - state.lastIn)  / 1048576.0) / dt;
        result.uploadMbps   = ((curOut - state.lastOut) / 1048576.0) / dt;
    }

    state.lastIn   = curIn;
    state.lastOut  = curOut;
    state.lastTime = now;
    return result;
}