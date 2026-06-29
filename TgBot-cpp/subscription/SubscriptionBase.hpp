#pragma once
// ═══════════════════════════════════════════════════════════
//  SubscriptionBase.hpp
//  Базовый класс подписки — профиль, хелперы
// ═══════════════════════════════════════════════════════════

#include <string>
#include <sstream>
#include <iomanip>
#include <tgbot/tgbot.h>
#include <nlohmann/json.hpp>
#include "ISubscription.hpp"

using json = nlohmann::json;

inline std::string drawLimitBar(int current, int max) {
    std::string bar = "<code>";
    for (int i = 0; i < max; ++i)
        bar += (i < current) ? "■" : "□";
    bar += "</code>";
    return bar;
}

class SubscriptionBase : public ISubscription {
public:
    std::string rankName  = "FREE";
    int         max_devices = 1;

    std::string getRankName() override { return rankName; }

    void displayProfile(TgBot::Bot& bot, TgBot::User::Ptr user,
                        const json& devices,
                        const std::string& photoPath = "",
                        long long chat_id = 0)
    {
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
            std::string subEnd;
            if (!devices.empty()
                && devices[0].contains("subscription_end")
                && !devices[0]["subscription_end"].is_null())
                subEnd = devices[0].value("subscription_end", "");
            if (!subEnd.empty())
                ss << "📅 <b>Подписка до:</b> <code>" << subEnd << "</code>\n";
        }

        ss << "<code>──────────────────────────</code>\n\n";

        TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);

        if (devices.empty()) {
            ss << "<i>   └ Список узлов пуст...</i>\n";
        } else {
            for (auto& dev : devices) {
                std::string name = dev.value("name", "Node");
                std::string hwid = dev.value("hwid", "");
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
        auto btnRef = std::make_shared<TgBot::InlineKeyboardButton>();
        btnRef->text         = "🔄 Обновить";
        btnRef->callbackData = "back_to_profile";
        row.push_back(btnRef);

        if (rankName != "OVERSEER") {
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
                } catch (...) {}
            }
            bot.getApi().sendMessage(target, ss.str(), nullptr, nullptr, keyboard, "HTML");
        } catch (const std::exception& e) {
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