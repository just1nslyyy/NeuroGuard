#pragma once
#include "ISubscription.hpp"
#include <iomanip>
#include <sstream>

std::string drawLimitBar(int current, int max) {
    std::string bar = "<code>";
    for (int i = 0; i < max; ++i)
        bar += (i < current) ? "■" : "□";
    bar += "</code>";
    return bar;
}

// ─── BASE ────────────────────────────────────────────────────────────────────
class SubscriptionBase : public ISubscription {
public:
    std::string rankName = "FREE";
    int max_devices = 1;

    std::string getRankName() override { return rankName; }

    // FIX: принимаем chat_id отдельно — он используется для отправки сообщения.
    // user->id может не совпадать с chat->id при вызове из callback.
    // Добавлен параметр chat_id; если 0 — используем user->id (старое поведение).
    void displayProfile(TgBot::Bot &bot, TgBot::User::Ptr user, const json &devices,
                        const std::string &photoPath = "",
                        long long chat_id = 0) {
        // Куда отправляем — chat_id приоритетнее
        long long target = (chat_id != 0) ? chat_id : (long long)user->id;

        std::stringstream ss;
        std::string rankEmoji = "👤";
        if (rankName == "SENTINEL") rankEmoji = "🛡️";
        if (rankName == "OVERSEER") rankEmoji = "👑";

        ss << "📟 <b>NEUROGUARD PROFILE</b>\n";
        ss << "<code>──────────────────────────</code>\n";
        ss << "👤 <b>User:</b> <code>"
           << (user->username.empty() ? user->firstName : "@" + user->username)
           << "</code>\n";
        ss << "🆔 <b>ID:</b>   <code>" << user->id << "</code>\n";
        ss << rankEmoji << " <b>Rank:</b> <b>" << rankName << "</b>\n";

        int devCount = (int)devices.size();
        ss << "🖥 <b>Nodes:</b> " << drawLimitBar(devCount, max_devices)
           << " <code>" << devCount << "/" << max_devices << "</code>\n";

        if (rankName != "FREE") {
            std::string subEnd = "";
            if (!devices.empty()
                && devices[0].contains("subscription_end")
                && !devices[0]["subscription_end"].is_null()) {
                subEnd = devices[0].value("subscription_end", "");
            }
            if (!subEnd.empty())
                ss << "📅 <b>Подписка до:</b> <code>" << subEnd << "</code>\n";
        }

        ss << "<code>──────────────────────────</code>\n\n";

        TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);

        if (devices.empty()) {
            ss << "<i>   └ Список узлов пуст...</i>\n";
        } else {
            for (auto &dev : devices) {
                std::string name = dev.value("name", "Node");
                std::string hwid = dev.value("hwid", "");

                // FIX: пропускаем устройства с пустым hwid — они сломают кнопку
                if (hwid.empty()) continue;

                ss << (dev.value("online", false) ? "🟢 " : "🔴 ")
                   << "<code>" << name << "</code>\n";

                auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                btn->text         = "⚙️ Настроить: " + name;
                btn->callbackData = "setup_" + hwid;
                keyboard->inlineKeyboard.push_back({btn});
            }
        }

        ss << "\n<code>──────────────────────────</code>\n";

        std::vector<TgBot::InlineKeyboardButton::Ptr> row;

        // FIX: "refresh_profile" заменён на "back_to_profile" — теперь
        // обработчик в main.cpp реально существует и перезагружает профиль
        auto btnRef = std::make_shared<TgBot::InlineKeyboardButton>();
        btnRef->text         = "🔄 Обновить";
        btnRef->callbackData = "back_to_profile";
        row.push_back(btnRef);

        if (rankName != "OVERSEER") {
            // FIX: "buy_menu" заменён на реальные обработчики
            auto btnUp = std::make_shared<TgBot::InlineKeyboardButton>();
            btnUp->text         = "💎 Upgrade";
            btnUp->callbackData = "show_sub_menu";
            row.push_back(btnUp);
        }
        keyboard->inlineKeyboard.push_back(row);

        try {
            if (!photoPath.empty()) {
                try {
                    bot.getApi().sendPhoto(
                        target,
                        TgBot::InputFile::fromFile(photoPath, "image/jpeg"),
                        ss.str(), 0, keyboard, "HTML");
                    return;
                } catch (...) {
                    // фото не найдено — падаём на текст
                }
            }
            bot.getApi().sendMessage(target, ss.str(), nullptr, nullptr, keyboard, "HTML");
        } catch (const std::exception &e) {
            // последний шанс — без клавиатуры
            try {
                bot.getApi().sendMessage(target, ss.str(), nullptr, nullptr, nullptr, "HTML");
            } catch (...) {}
        }
    }

protected:
    std::string renderBar(float percent) {
        int filled = (int)(percent / 10.0f);
        if (filled > 10) filled = 10;
        if (filled < 0)  filled = 0;
        std::string bar = "<code>";
        for (int i = 0; i < 10; ++i) bar += (i < filled) ? "■" : "□";
        bar += "</code>";
        return bar;
    }

    std::string formatUptime(int seconds) {
        if (seconds <= 0) return "0м";
        int h = seconds / 3600;
        int m = (seconds % 3600) / 60;
        if (h > 0) return std::to_string(h) + "ч " + std::to_string(m) + "м";
        return std::to_string(m) + "м";
    }

    std::string formatDouble(double val) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << val;
        return oss.str();
    }
};

// ─── FREE ─────────────────────────────────────────────────────────────────────
class FreeSub : public SubscriptionBase {
public:
    FreeSub() { rankName = "FREE"; max_devices = 1; }

    std::string formatStatus(const json &dev) override {
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

// ─── SENTINEL ─────────────────────────────────────────────────────────────────
class SentinelSub : public SubscriptionBase {
public:
    SentinelSub() { rankName = "SENTINEL"; max_devices = 3; }
    bool canControlPower()    override { return true;  }
    bool canTakeScreenshots() override { return false; }
    bool canKillProcesses()   override { return false; }

    std::string formatStatus(const json &dev) override {
        std::stringstream ss;
        bool online = dev.value("online", false);
        std::string name = dev.value("name","PC");
        ss << (online ? "🟢 " : "🔴 ") << "<b>" << name << "</b>\n";
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
        {
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
                auto &disks = dev["disks"];
                if (!disks.empty()) {
                    ss << "💾 <b>Диски:</b>\n";
                    for (size_t i = 0; i < disks.size(); ++i) {
                        auto  &d     = disks[i];
                        float  pct   = d.value("percent",0.0f);
                        float  total = d.value("total",  0.0f);
                        float  used  = d.value("used",   0.0f);
                        float  fr    = total - used;
                        std::string pfx = (i==disks.size()-1) ? "└ " : "├ ";
                        ss << pfx << d.value("name","?") << ": "
                           << renderBar(pct) << " <code>" << (int)pct << "%</code>"
                           << " Своб: <code>" << formatDouble(fr) << " GB</code>\n";
                    }
                    ss << "\n";
                }
            }
            ss << "🔋 <b>Питание:</b> <code>" << dev.value("power_source","N/A") << "</code>";
            if (dev.contains("battery_pct"))
                ss << " (<code>" << dev.value("battery_pct",0) << "%</code>)";
            ss << "\n";
        }
        ss << "━━━━━━━━━━━━━━━━━━\n";
        ss << "⏱ <b>Аптайм:</b> <code>" << formatUptime(dev.value("uptime",0)) << "</code>\n";
        if (dev.contains("subscription_end") && !dev["subscription_end"].is_null())
            ss << "📅 <b>Подписка до:</b> <code>" << dev.value("subscription_end","N/A") << "</code>\n";
        ss << "\n👑 <i>Нужны скриншоты и процессы?</i>\n👉 /subscribe";
        return ss.str();
    }
};

// ─── OVERSEER ─────────────────────────────────────────────────────────────────
class OverseerSub : public SubscriptionBase {
public:
    OverseerSub() { rankName = "OVERSEER"; max_devices = 10; }
    bool canControlPower()    override { return true; }
    bool canTakeScreenshots() override { return true; }
    bool canKillProcesses()   override { return true; }

    std::string formatStatus(const json &data) override {
        std::stringstream ss;
        bool online = data.value("online", false);
        std::string name = data.value("name","New Device");
        float cpuLoad = data.value("cpu",      0.0f);
        float cpuTemp = data.value("cpu_temp", 0.0f);
        float gpuLoad = data.value("gpu",      0.0f);
        float gpuTemp = data.value("gpu_temp", 0.0f);
        int   ramPct  = (int)data.value("ram", 0.0f);

        ss << (online ? "🟢 " : "🔴 ") << "<b>" << name << "</b>\n";
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
        {
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
                        std::string prefix = (i==cores.size()-1) ? "└ " : "│ ";
                        ss << "<code>" << prefix
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
                auto &disks = data["disks"];
                for (size_t i = 0; i < disks.size(); ++i) {
                    auto  &disk  = disks[i];
                    float  pct   = disk.value("percent",0.0f);
                    float  total = disk.value("total",  0.0f);
                    float  used  = disk.value("used",   0.0f);
                    float  fr    = total - used;
                    std::string pfx = (i==disks.size()-1) ? "└ " : "├ ";
                    ss << pfx << disk.value("name","?") << ": "
                       << renderBar(pct) << " <code>" << (int)pct << "%</code>"
                       << " Своб: <code>" << formatDouble(fr) << " GB</code>\n";
                }
            }
        }
        ss << "━━━━━━━━━━━━━━━━━━\n";
        ss << "🔌 <b>Питание:</b> <code>" << data.value("power_source","AC")
           << " (" << (int)data.value("battery_pct",100) << "%)</code>\n";
        ss << "⏱ <b>Аптайм:</b> <code>" << formatUptime(data.value("uptime",0)) << "</code>\n";
        if (data.contains("subscription_end") && !data["subscription_end"].is_null())
            ss << "📅 <b>Подписка до:</b> <code>" << data.value("subscription_end","N/A") << "</code>\n";
        return ss.str();
    }
};