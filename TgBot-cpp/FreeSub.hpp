#pragma once
#include "ISubscription.hpp"

class FreeSub : public SubscriptionBase {
public:
    FreeSub() { rankName = "FREE"; max_devices = 1; }

    // На Free только просмотр базовых метрик
    bool canControlPower() override { return false; }
    bool canTakeScreenshots() override { return false; }
    bool canKillProcesses() override { return false; }

    std::string formatStatus(const json& data) override {
        std::string res = "🆓 <b>RANK: " + getRankName() + "</b>\n";
        res += "🖥 <b>" + data.value("name", "Device") + "</b> [" + (data.value("online", false) ? "🟢" : "🔴") + "]\n";
        res += "━━━━━━━━━━━━━━━━━━\n";

        if (data.value("online", false)) {
            // CPU
            res += "🖥 <b>CPU Load:</b>\n";
            res += renderBar(data.value("cpu", 0.0)) + " <code>" + std::to_string((int)data.value("cpu", 0.0)) + "%</code>\n\n";

            // GPU
            res += "🎮 <b>GPU Load:</b>\n";
            res += renderBar(data.value("gpu", 0.0)) + " <code>" + std::to_string((int)data.value("gpu", 0.0)) + "%</code>\n\n";

            // RAM
            res += "🧠 <b>RAM Load:</b>\n";
            res += renderBar(data.value("ram", 0.0)) + " <code>" + std::to_string((int)data.value("ram", 0.0)) + "%</code>\n";
        } else {
            res += "⏳ <i>Устройство офлайн</i>\n";
        }

        res += "━━━━━━━━━━━━━━━━━━\n";
        res += "⏱ Uptime: <code>" + formatUptime(data.value("uptime", 0)) + "</code>\n\n";

        // Вместо замочков в каждой строке — один четкий призыв
        res += "💡 <b>Хочешь больше функций и управление?</b>\n";
        res += "👉 /subscribe";

        return res;
    }

private:
    std::string renderBar(float percent) {
        int filled = (int)(percent / 10);
        if (filled > 10) filled = 10;
        if (filled < 0) filled = 0;

        // Используем <code> для идеального выравнивания баров
        std::string bar = "<code>";
        for(int i=0; i<10; ++i) bar += (i < filled) ? "■" : "░";
        bar += "</code>";
        return bar;
    }

    std::string formatUptime(int seconds) {
        int h = seconds / 3600;
        int m = (seconds % 3600) / 60;
        return std::to_string(h) + "ч " + std::to_string(m) + "м";
    }
};