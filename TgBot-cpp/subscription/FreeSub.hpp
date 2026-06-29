#pragma once
// ═══════════════════════════════════════════════════════════
//  FreeSub.hpp — FREE тариф
// ═══════════════════════════════════════════════════════════

#include "SubscriptionBase.hpp"

class FreeSub : public SubscriptionBase {
public:
    FreeSub() { rankName = "FREE"; max_devices = 1; }

    bool canControlPower()    override { return false; }
    bool canTakeScreenshots() override { return false; }
    bool canKillProcesses()   override { return false; }

    std::string formatStatus(const json& dev) override {
        std::stringstream ss;
        bool online = dev.value("online", false);
        ss << "🆓 <b>RANK: FREE</b>\n";
        ss << "🖥 <b>" << dev.value("name","PC") << "</b> ["
           << (online ? "🟢 Онлайн" : "🔴 Оффлайн") << "]\n";
        ss << "━━━━━━━━━━━━━━━━━━\n";
        if (online) {
            float cpu = dev.value("cpu", 0.0f);
            float gpu = dev.value("gpu", 0.0f);
            float ram = dev.value("ram", 0.0f);
            ss << "🖥 CPU: " << renderBar(cpu) << " <code>" << (int)cpu << "%</code>\n";
            ss << "🎮 GPU: " << renderBar(gpu) << " <code>" << (int)gpu << "%</code>\n";
            ss << "🧠 RAM: " << renderBar(ram) << " <code>" << (int)ram << "%</code>\n";
        } else {
            ss << "<i>⏳ Устройство офлайн</i>\n";
        }
        ss << "━━━━━━━━━━━━━━━━━━\n";
        ss << "⏱ Аптайм: <code>" << formatUptime(dev.value("uptime",0)) << "</code>\n";
        ss << "🕒 Обновление: <code>" << dev.value("last_seen_time","N/A") << "</code>\n\n";
        ss << "💡 <i>Хочешь больше функций?</i>\n👉 /subscribe";
        return ss.str();
    }
};