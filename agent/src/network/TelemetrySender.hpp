#pragma once
#define NOMINMAX
// -*- coding: utf-8 -*-
// ═══════════════════════════════════════════════════════════
//  TelemetrySender.hpp
//  Отправка телеметрии на бэкенд
// ═══════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <string>
#include <vector>
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include "../hardware/OhmProvider.hpp"
#include "../hardware/CpuMonitor.hpp"
#include "../hardware/GpuMonitor.hpp"
#include "../hardware/RamMonitor.hpp"
#include "../hardware/DiskMonitor.hpp"
#include "../hardware/NetworkMonitor.hpp"
#include "../system/PowerManager.hpp"
#include "../system/ProcessManager.hpp"

using json = nlohmann::json;

struct OhmData {
    float       cpuTemp = 0.0f;
    float       cpuLoad = 0.0f;
    float       gpuTemp = 0.0f;
    float       gpuLoad = 0.0f;
    std::string cpuName;
    std::string gpuName;
};

struct TelemetryPayload {
    std::string              hwid;
    OhmData                  ohm;
    RamData                  ram;
    std::vector<DiskData>    disks;
    PowerData                power;
    NetSpeed                 net;
    std::vector<ProcessInfo> processes;
    std::vector<int>         coreLoads;
    double                   sessionUptime = 0.0;
    double                   estimatedKwh  = 0.0;
    float                    cpuTempAvg    = 0.0f;
    float                    cpuTempMax    = 0.0f;
    float                    gpuTempAvg    = 0.0f;
    float                    gpuTempMax    = 0.0f;
};

struct TelemetryResponse {
    std::string rank    = "free";
    std::string command = "none";
    bool        valid   = false;
};

inline TelemetryResponse sendTelemetry(
    const TelemetryPayload& p,
    const std::string&      serverUrl,
    bool&                   shouldStop)
{
    json j;
    j["hwid"]           = p.hwid;
    j["cpu_load"] = p.ohm.cpuLoad < 0.f ? 0.f : (p.ohm.cpuLoad > 100.f ? 100.f : p.ohm.cpuLoad);
    j["cpu_temp"] = p.ohm.cpuTemp;
    j["cpu_model"] = p.ohm.cpuName.empty() ? std::string("N/A") : p.ohm.cpuName;
    j["cpu_cores"] = p.coreLoads;
    j["cpu_temp_avg"] = p.cpuTempAvg;
    j["cpu_temp_max"] = p.cpuTempMax;
    j["gpu_load"] = p.ohm.gpuLoad < 0.f ? 0.f : (p.ohm.gpuLoad > 100.f ? 100.f : p.ohm.gpuLoad);
    j["gpu_temp"] = p.ohm.gpuTemp;
    j["gpu_model"] = p.ohm.gpuName.empty() ? std::string("N/A") : p.ohm.gpuName;
    j["gpu_temp_avg"] = p.gpuTempAvg;
    j["gpu_temp_max"] = p.gpuTempMax;
    j["ram_load"] = p.ram.percent < 0 ? 0.f : (p.ram.percent > 100 ? 100.f : (float)p.ram.percent);
    j["ram_used"] = p.ram.usedGb;
    j["ram_total"] = p.ram.totalGb;
    j["power_source"] = p.power.source;
    j["battery_pct"] = p.power.batteryPercent < 0 ? 0 : (p.power.batteryPercent > 100 ? 100 : p.power.batteryPercent);
    j["uptime"] = (int)p.power.uptime;
    j["disk_model"] = getDiskModel();
    j["net_down"] = p.net.downloadMbps;
    j["net_up"] = p.net.uploadMbps;
    j["session_uptime"] = p.sessionUptime;
    j["estimated_kwh"] = p.estimatedKwh;

    j["disks"] = json::array();
    for (auto& d : p.disks) {
        std::string name = d.name;
        while (!name.empty() &&
               (name.back() == '\\' || name.back() == '/'))
            name.pop_back();
        json disk;
        disk["drive_name"] = name;
        disk["total_gb"]   = std::round(d.totalGb * 10.f) / 10.f;
        disk["used_gb"]    = std::round(d.usedGb  * 10.f) / 10.f;
        float pct = d.percent < 0 ? 0.f : (d.percent > 100 ? 100.f : (float)d.percent);
        disk["percent"] = pct;
        j["disks"].push_back(disk);
    }

    j["top_processes"] = json::array();
    for (auto& proc : p.processes) {
        json pr;
        pr["name"] = proc.name;
        pr["pid"]  = proc.pid;
        pr["ram"]  = (float)proc.memoryMb;
        float cpu = proc.cpuUsage < 0.f ? 0.f : (proc.cpuUsage > 100.f ? 100.f : proc.cpuUsage);
        pr["cpu"] = cpu;
        j["top_processes"].push_back(pr);
    }

    std::string body = j.dump();

    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt)
            std::this_thread::sleep_for(std::chrono::seconds(2));
        try {
            auto r = cpr::Post(
                cpr::Url{serverUrl + "/telemetry"},
                cpr::Body{body},
                cpr::Header{
                    {"Content-Type", "application/json"},
                    {"ngrok-skip-browser-warning", "true"}
                },
                cpr::Timeout{8000});

            if (r.status_code == 200) {
                auto rj = json::parse(r.text);
                TelemetryResponse resp;
                resp.valid   = true;
                resp.rank    = rj.value("rank",    "free");
                resp.command = rj.value("command", "none");
                return resp;
            }
            if (r.status_code == 401) {
                std::cerr << "[WARN] 401 — устройство отвязано\n";
                shouldStop = true;
                return {};
            }
            std::cerr << "[ERR] Backend " << r.status_code << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[ERR] attempt " << attempt + 1
                      << ": " << e.what() << "\n";
        }
    }
    return {};
}