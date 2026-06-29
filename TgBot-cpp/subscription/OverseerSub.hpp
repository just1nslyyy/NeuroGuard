#pragma once
// ═══════════════════════════════════════════════════════════
//  OverseerSub.hpp — OVERSEER тариф
// ═══════════════════════════════════════════════════════════

#include "SubscriptionBase.hpp"
#include <iomanip>

class OverseerSub : public SubscriptionBase {
public:
    OverseerSub() { rankName = "OVERSEER"; max_devices = 10; }

    bool canControlPower()    override { return true; }
    bool canTakeScreenshots() override { return true; }
    bool canKillProcesses()   override { return true; }

    std::string formatStatus(const json& data) override {
        std::stringstream ss;
        bool online   = data.value("online", false);
        float cpuLoad = data.value("cpu",      0.0f);
        float cpuTemp = data.value("cpu_temp", 0.0f);
        float gpuLoad = data.value("gpu",      0.0f);
        float gpuTemp = data.value("gpu_temp", 0.0f);
        int   ramPct  = (int)data.value("ram", 0.0f);

        ss << (online ? "🟢 " : "🔴 ") << "<b>" << data.value("name","PC") << "</b>\n";
        ss << "🌐 <b>Сеть:</b> <code>" << (online ? "ONLINE" : "OFFLINE") << "</code>\n";

        if (online) {
            if      (cpuLoad>90||ramPct>90||cpuTemp>85||gpuTemp>85)
                ss << "📊 <b>Состояние:</b> 🔴 <code>КРИТИЧЕСКИЙ УРОВЕНЬ</code>\n";
            else if (cpuLoad>60||ramPct>75||cpuTemp>75||gpuTemp>75)
                ss << "📊 <b>Состояние:</b> 🟡 <code>ВЫСОКАЯ НАГРУЗКА</code>\n";
            else
                ss << "📊 <b>Состояние:</b> 🟢 <code>СИСТЕМА СТАБИЛЬНА</code>\n";
        }

        ss << "🕒 <b>Замер:</b> <code>" << data.value("last_seen_time","--:--:--") << "</code>\n";
        ss << "━━━━━━━━━━━━━━━━━━\n";
        if (!online) ss << "<i>📋 Данные последнего замера:</i>\n";

        ss << "🖥 <b>CPU:</b> <i>" << data.value("cpu_name","N/A") << "</i>\n";
        ss << "├ Нагрузка: <code>" << (int)cpuLoad << "%</code> | 🌡 <code>" << (int)cpuTemp << "°C</code>\n";

        if (data.contains("cores") && data["cores"].is_array()) {
            auto cores = data["cores"];
            if (!cores.empty()) {
                ss << "├ <b>Ядра:</b>\n";
                for (size_t i = 0; i < cores.size(); ++i) {
                    int cLoad = 0;
                    if (cores[i].is_number()) cLoad = (int)cores[i].get<float>();
                    cLoad = std::max(0, std::min(100, cLoad));
                    std::string pfx = (i==cores.size()-1) ? "└ " : "│ ";
                    ss << "<code>" << pfx
                       << std::setw(2) << i << ": "
                       << renderBar((float)cLoad) << " " << cLoad << "%</code>\n";
                }
            }
        }
        ss << "\n";

        ss << "🎮 <b>GPU:</b> <i>" << data.value("gpu_name","N/A") << "</i>\n";
        ss << "├ Нагрузка: " << renderBar(gpuLoad) << " <code>" << (int)gpuLoad << "%</code>\n";
        ss << "└ Темп: 🌡 <code>" << (int)gpuTemp << "°C</code>\n\n";

        float ramUsed  = data.value("ram_used",  0.0f);
        float ramTotal = data.value("ram_total", 0.0f);
        ss << "🧠 <b>RAM:</b> " << renderBar((float)ramPct)
           << " <code>" << formatDouble(ramUsed) << " / " << formatDouble(ramTotal)
           << " GB (" << ramPct << "%)</code>\n\n";

        ss << "🌐 <b>Трафик:</b>\n";
        ss << "<code>⬇️ Входящий:  " << formatDouble(data.value("net_down",0.0)) << " MB/s</code>\n";
        ss << "<code>⬆️ Исходящий: " << formatDouble(data.value("net_up",  0.0)) << " MB/s</code>\n\n";

        ss << "💾 <b>Диски:</b>\n";
        ss << "├ Модель: <code>" << data.value("disk_name","N/A") << "</code>\n";
        if (data.contains("disks") && data["disks"].is_array()) {
            auto& disks = data["disks"];
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
        }

        ss << "━━━━━━━━━━━━━━━━━━\n";
        ss << "🔌 <b>Питание:</b> <code>" << data.value("power_source","AC")
           << " (" << (int)data.value("battery_pct",100) << "%)</code>\n";
        ss << "⏱ <b>Аптайм:</b> <code>" << formatUptime(data.value("uptime",0)) << "</code>\n";
        if (data.contains("subscription_end") && !data["subscription_end"].is_null())
            ss << "📅 <b>Подписка до:</b> <code>"
               << data.value("subscription_end","N/A") << "</code>\n";
        return ss.str();
    }
};