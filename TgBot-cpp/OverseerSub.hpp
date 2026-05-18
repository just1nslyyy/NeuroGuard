#pragma once
#include "ISubscription.hpp"
#include <iomanip>
#include <sstream>

class OverseerSub : public SubscriptionBase {
public:
    OverseerSub() { rankName = "OVERSEER"; max_devices = 10; }

    bool canControlPower() override { return true; }
    bool canTakeScreenshots() override { return true; }
    bool canKillProcesses() override { return true; }

    std::string formatStatus(const json& data) override {
        std::stringstream ss;
        bool online = data.value("online", false);
        std::string name = data.value("name", "New Device");

        ss << (online ? "🟢 " : "🔴 ") << "<b>" << name << "</b>\n";
        ss << "🌐 <b>Сеть:</b> <code>" << (online ? "ONLINE" : "OFFLINE") << "</code>\n";

        loat cpuLoad = data.value("cpu", 0.0f);
        float cpuTemp = data.value("cpu_temp", 0.0f);
        float gpuTemp = data.value("gpu_temp", 0.0f);
        int ramPct = (int)data.value("ram", 0);

        // Логика определения статуса
        if (cpuLoad > 90.0f || ramPct > 90 || cpuTemp > 85.0f || gpuTemp > 85.0f) {
            ss << "📊 <b>Состояние:</b> 🔴 <code>КРИТИЧЕСКИЙ УРОВЕНЬ</code>\n";
        }
        else if (cpuLoad > 60.0f || ramPct > 75 || cpuTemp > 75.0f || gpuTemp > 75.0f) {
            ss << "📊 <b>Состояние:</b> 🟡 <code>ВЫСОКАЯ НАГРУЗКА</code>\n";
        }
        else {
            ss << "📊 <b>Состояние:</b> 🟢 <code>СИСТЕМА СТАБИЛЬНА</code>\n";
        }

        ss << "🕒 <b>Замер:</b> <code>" << data.value("last_seen_time", "--:--:--") << "</code>\n";
        ss << "━━━━━━━━━━━━━━━━━━\n";

        if (online) {
            // --- CPU SECTION ---
            ss << "🖥 <b>CPU:</b> <i>" << data.value("cpu_name", "N/A") << "</i>\n";
            ss << "├ Avg: <code>" << (int)cpuLoad << "%</code> | 🌡 <code>" << (int)data.value("cpu_temp", 0) << "°C</code>\n";

            if (data.contains("cores") && data["cores"].is_array()) {
                ss << "├ <b>Cores Load:</b>\n";
                auto cores = data["cores"];
                for (size_t i = 0; i < cores.size(); ++i) {
                    float cLoad = cores[i].get<float>();
                    std::string prefix = (i == cores.size() - 1) ? "└ " : "│ ";
                    ss << "<code>" << prefix << i << ": " << renderBar(cLoad) << " " << (int)cLoad << "%</code>\n";
                }
            }
            ss << "\n";

            // --- GPU SECTION ---
            ss << "🎮 <b>GPU:</b> <i>" << data.value("gpu_name", "N/A") << "</i>\n";
            ss << "├ Load: " << renderBar(data.value("gpu", 0.0f)) << " <code>" << (int)data.value("gpu", 0.0f) << "%</code>\n";
            ss << "└ Temp: 🌡 <code>" << (int)data.value("gpu_temp", 0) << "°C</code>\n\n";

            // --- RAM SECTION ---
            float ramUsed = data.value("ram_used", 0.0f);
            float ramTotal = data.value("ram_total", 0.0f);
            int ramPct = (int)data.value("ram", 0.0f);
            ss << "🧠 <b>RAM:</b> <code>" << formatDouble(ramUsed) << " / " << formatDouble(ramTotal) << " GB (" << ramPct << "%)</code>\n\n";

            // --- NETWORK SECTION (EXCLUSVE) ---
            ss << "🌐 <b>NETWORK TRAFFIC:</b>\n";
            ss << "<code>⬇️ Down: " << formatDouble(data.value("net_down", 0.0)) << " MB/s</code>\n";
            ss << "<code>⬆️ Up:   " << formatDouble(data.value("net_up", 0.0)) << " MB/s</code>\n\n";

            // --- STORAGE SECTION ---
            ss << "💾 <b>Storage:</b>\n";
            ss << "├ HW: <code>" << data.value("disk_name", "N/A") << "</code>\n";
            if (data.contains("disks") && data["disks"].is_array()) {
                for (auto& disk : data["disks"]) {
                    ss << "└ " << disk.value("name", "?") << ": " << renderBar(disk.value("percent", 0.0f)) << " <code>" << (int)disk.value("percent", 0) << "%</code>\n";
                }
            }
        }

        ss << "━━━━━━━━━━━━━━━━━━\n";
        ss << "🔌 <b>Power:</b> <code>" << data.value("power_source", "AC") << " (" << (int)data.value("battery_pct", 100) << "%)</code>\n";
        ss << "⏱ <b>Uptime:</b> <code>" << formatUptime(data.value("uptime", 0)) << "</code>";

        return ss.str();
    }

private:
    std::string formatDouble(double val) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << val;
        return oss.str();
    }
};