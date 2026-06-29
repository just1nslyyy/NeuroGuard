#pragma once
// ═══════════════════════════════════════════════════════════
//  SentinelSub.hpp — SENTINEL тариф
// ═══════════════════════════════════════════════════════════

#include "SubscriptionBase.hpp"

class SentinelSub : public SubscriptionBase {
public:
    SentinelSub() { rankName = "SENTINEL"; max_devices = 3; }

    bool canControlPower()    override { return true;  }
    bool canTakeScreenshots() override { return false; }
    bool canKillProcesses()   override { return false; }

    std::string formatStatus(const json& dev) override {
        std::stringstream ss;
        bool online = dev.value("online", false);
        ss << (online ? "🟢 " : "🔴 ") << "<b>" << dev.value("name","PC") << "</b>\n";
        ss << "🌐 <b>Сеть:</b> <code>" << (online ? "ONLINE" : "OFFLINE") << "</code>\n";

        if (online) {
            float cpu     = dev.value("cpu",      0.0f);
            float cpuTemp = dev.value("cpu_temp", 0.0f);
            float gpuTemp = dev.value("gpu_temp", 0.0f);
            int   ram     = (int)dev.value("ram",  0.0f);
            if      (cpu>90||ram>90||cpuTemp>85||gpuTemp>85)
                ss << "📊 <b>Состояние:</b> 🔴 <code>КРИТИЧЕСКИЙ УРОВЕНЬ</code>\n";
            else if (cpu>60||ram>75||cpuTemp>75||gpuTemp>75)
                ss << "📊 <b>Состояние:</b> 🟡 <code>ВЫСОКАЯ НАГРУЗКА</code>\n";
            else
                ss << "📊 <b>Состояние:</b> 🟢 <code>СИСТЕМА СТАБИЛЬНА</code>\n";
        }

        ss << "🕒 <b>Замер:</b> <code>" << dev.value("last_seen_time","--:--:--") << "</code>\n";
        ss << "━━━━━━━━━━━━━━━━━━\n";

        float cpu     = dev.value("cpu",      0.0f);
        float cpuTemp = dev.value("cpu_temp", 0.0f);
        float gpu     = dev.value("gpu",      0.0f);
        float gpuTemp = dev.value("gpu_temp", 0.0f);
        float ramUsed  = dev.value("ram_used",  0.0f);
        float ramTotal = dev.value("ram_total", 0.0f);
        int   ramPct   = (int)dev.value("ram",  0.0f);

        if (!online) ss << "<i>📋 Данные последнего замера:</i>\n";
        ss << "🖥 <b>CPU:</b> <i>" << dev.value("cpu_name","N/A") << "</i>\n";
        ss << "🔥 Нагрузка: " << renderBar(cpu) << " <code>" << (int)cpu << "%</code>\n";
        ss << "🌡 Температура: <code>" << (int)cpuTemp << "°C</code>\n\n";
        ss << "🎮 <b>GPU:</b> <i>" << dev.value("gpu_name","N/A") << "</i>\n";
        ss << "   Нагрузка: " << renderBar(gpu) << " <code>" << (int)gpu << "%</code>\n";
        ss << "🌡 Температура: <code>" << (int)gpuTemp << "°C</code>\n\n";
        ss << "🧠 <b>RAM:</b> " << renderBar((float)ramPct)
           << " <code>" << formatDouble(ramUsed) << " / " << formatDouble(ramTotal)
           << " GB (" << ramPct << "%)</code>\n\n";

        if (dev.contains("disks") && dev["disks"].is_array()) {
            auto& disks = dev["disks"];
            if (!disks.empty()) {
                ss << "💾 <b>Диски:</b>\n";
                for (size_t i = 0; i < disks.size(); ++i) {
                    auto&  d   = disks[i];
                    float  pct = d.value("percent",0.0f);
                    float  tot = d.value("total",  0.0f);
                    float  usd = d.value("used",   0.0f);
                    std::string pfx = (i==disks.size()-1) ? "└ " : "├ ";
                    ss << pfx << d.value("name","?") << ": "
                       << renderBar(pct) << " <code>" << (int)pct << "%</code>"
                       << " Своб: <code>" << formatDouble(tot-usd) << " GB</code>\n";
                }
                ss << "\n";
            }
        }

        ss << "🔋 <b>Питание:</b> <code>" << dev.value("power_source","N/A") << "</code>";
        if (dev.contains("battery_pct"))
            ss << " (<code>" << dev.value("battery_pct",0) << "%</code>)";
        ss << "\n";
        ss << "━━━━━━━━━━━━━━━━━━\n";
        ss << "⏱ <b>Аптайм:</b> <code>" << formatUptime(dev.value("uptime",0)) << "</code>\n\n";
        ss << "👑 <i>Нужны скриншоты и процессы?</i>\n👉 /subscribe";
        return ss.str();
    }
};