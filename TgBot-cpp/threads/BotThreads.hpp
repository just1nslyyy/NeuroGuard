#pragma once
// ═══════════════════════════════════════════════════════════
//  BotThreads.hpp
//  Фоновые потоки — авто-сон, алерты подписок
// ═══════════════════════════════════════════════════════════

#include <thread>
#include <chrono>
#include <atomic>
#include <ctime>
#include <tgbot/tgbot.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "../config/BotConfig.hpp"
#include "../state/BotState.hpp"
#include "../utils/BotHelpers.hpp"

inline void autoSleepThread(TgBot::Bot& bot, std::atomic<bool>& running) {
    logInfo("autoSleepThread", "Started");
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        try {
            auto now = std::time(nullptr);
            auto msk = now + 3 * 3600;
            auto* tu = std::gmtime(&now);
            int ch = (tu->tm_hour + 3) % 24, cm = tu->tm_min;
            auto* td = std::gmtime(&msk);
            char db[16];
            std::strftime(db, sizeof(db), "%Y-%m-%d", td);
            std::string today(db);

            std::map<long long, std::pair<std::string,std::string>> schedSnap;
            std::map<long long, std::string> firedSnap;
            {
                std::lock_guard<std::mutex> lock(globalMutex);
                schedSnap  = autoSleepSchedule;
                firedSnap  = autoSleepFiredDate;
            }

            for (auto& [tid, sch] : schedSnap) {
                if (!running) break;
                try {
                    auto [hwid, ts] = sch;
                    auto [sh, sm]   = parseTime(ts);
                    if (sh < 0 || ch != sh || cm != sm) continue;
                    if (firedSnap.count(tid) && firedSnap[tid] == today) continue;

                    cpr::Post(
                        cpr::Url{API_URL + "/bot/set_command"},
                        cpr::Parameters{
                            {"hwid", hwid}, {"command", "sleep"}, {"tg_id", std::to_string(tid)}
                        },
                        cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                        cpr::Timeout{5000});

                    TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);
                    auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                    btn->text         = "❌ Отменить сон";
                    btn->callbackData = "cancel_sleep_" + hwid;
                    kb->inlineKeyboard.push_back({btn});

                    bot.getApi().sendMessage(tid,
                        "🌙 <b>Авто-сон:</b> ПК <code>" + hwid.substr(0,8) + "...</code>\n"
                        "Время: <b>" + ts + " МСК</b>",
                        nullptr, nullptr, kb, "HTML");

                    {
                        std::lock_guard<std::mutex> lock(globalMutex);
                        autoSleepFiredDate[tid] = today;
                    }
                } catch (const std::exception& e) {
                    logError("autoSleepThread sendMessage", e.what());
                }
            }
        } catch (const std::exception& e) {
            logError("autoSleepThread", e.what());
        }
    }
}

inline void subscriptionAlertThread(TgBot::Bot& bot, std::atomic<bool>& running) {
    logInfo("subscriptionAlertThread", "Started");
    while (running) {
        std::this_thread::sleep_for(std::chrono::hours(12));
        if (!running) break;
        try {
            auto r = cpr::Get(
                cpr::Url{API_URL + "/bot/expiring_subscriptions"},
                cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                cpr::Timeout{10000});
            if (r.status_code != 200) continue;

            auto alerts = nlohmann::json::parse(r.text);
            for (auto& e : alerts) {
                if (!running) break;
                try {
                    long long tid = e.value("tg_id", 0LL);
                    if (!tid) continue;
                    bot.getApi().sendMessage(tid,
                        "⏰ <b>Подписка заканчивается!</b>\n\n"
                        "Истекает <b>" + e.value("end_date", std::string("?")) + "</b>"
                        " (через <b>" + std::to_string(e.value("days_left", 0)) + " дн.</b>)\n\n"
                        "👉 /subscribe",
                        nullptr, nullptr, nullptr, "HTML");
                } catch (const std::exception& ex) {
                    logError("subscriptionAlertThread sendMessage", ex.what());
                }
            }
        } catch (const std::exception& e) {
            logError("subscriptionAlertThread", e.what());
        }
    }
}