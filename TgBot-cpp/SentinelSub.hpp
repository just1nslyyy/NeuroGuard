#pragma once
#include "ISubscription.hpp"

class SentinelSub1 : public ISubscription {
public:
    std::string getRankName() override { return "SENTINEL"; }

    // Sentinel: Питание — да, Скриншоты/Процессы — нет
    bool canControlPower() override { return true; }
    bool canTakeScreenshots() override { return false; }
    bool canKillProcesses() override { return false; }
    int getHealthDays()       override { return 7;  }


    std::string formatStatus(const json& data) override {
        std::string res = "🛡 <b>RANK: " + getRankName() + "</b>\n";
        res += "🖥 <b>" + data.value("name", "Device") + "</b> [" + data.value("status_label", "") + "]\n";
        res += "━━━━━━━━━━━━━━━━━━\n";

        // Нагрузка с барами (используем <code> для ровности)
        res += "🔥 CPU: " + renderBar(data.value("cpu", 0.0)) + " <code>" + std::to_string((int)data.value("cpu", 0.0)) + "%</code>\n";
        res += "🌡 Temp: <code>" + std::to_string((int)data.value("cpu_temp", 0.0)) + "°C</code>\n\n";

        res += "🧠 RAM: " + renderBar(data.value("ram", 0.0)) + " <code>" + std::to_string((int)data.value("ram", 0.0)) + "%</code>\n";

        // Специфичная инфа для Sentinel — питание и аптайм
        res += "\n🔋 Питание: <code>" + data.value("power_source", "N/A") + "</code>";
        if (data.contains("battery_pct")) {
            res += " (<code>" + std::to_string(data.value("battery_pct", 0)) + "%</code>)";
        }

        res += "\n⏱ Uptime: <code>" + formatUptime(data.value("uptime", 0)) + "</code>\n";
        res += "━━━━━━━━━━━━━━━━━━\n";

        // Рекламный блок вместо замочков
        res += "👑 <b>Нужны скриншоты и управление процессами?</b>\n";
        res += "👉 /subscribe";

        return res;
    }

private:
    // Переопределяем renderBar, чтобы добавить <code> именно здесь
    std::string renderBar(float percent) {
        int filled = (int)(percent / 10);
        if (filled > 10) filled = 10;
        if (filled < 0) filled = 0;
        std::string bar = "<code>";
        for(int i=0; i<10; ++i) bar += (i < filled) ? "■" : "□";
        bar += "</code>";
        return bar;
    }

    std::string formatUptime(int seconds) {
        int h = seconds / 3600;
        int m = (seconds % 3600) / 60;
        return std::to_string(h) + "ч " + std::to_string(m) + "м";
    }
};