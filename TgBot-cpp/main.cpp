// ═══════════════════════════════════════════════════════════
//  main.cpp
//  NeuroGuard Bot — точка входа
// ═══════════════════════════════════════════════════════════

#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <tgbot/tgbot.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include "config/BotConfig.hpp"
#include "state/BotState.hpp"
#include "utils/BotHelpers.hpp"
#include "ui/Keyboards.hpp"
#include "subscription/SubFactory.hpp"
#include "threads/BotThreads.hpp"
#include "handlers/CommandHandlers.hpp"
#include "handlers/CallbackHandlers.hpp"
#include "handlers/MessageHandlers.hpp"
#include "admin/AdminPanel.hpp"

using namespace std;
using namespace TgBot;

// ── Профиль ─────────────────────────────────────────────────
inline void showMesProfile(TgBot::Bot& bot, TgBot::Message::Ptr message) {
    long long cid = message->chat->id;
    auto r = cpr::Get(cpr::Url{API_URL + "/bot/status?tg_id=" + to_string(cid)},
        cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
    if (r.status_code != 200) {
        bot.getApi().sendMessage(cid, "❌ Сервер недоступен.");
        return;
    }
    try {
        auto j = nlohmann::json::parse(r.text);
        string rank = j.value("user_rank", "FREE");
        shared_ptr<ISubscription> sub;
        {
            lock_guard<mutex> lock(globalMutex);
            sub = SubFactory::create(rank);
            if (!sub) { bot.getApi().sendMessage(cid, "❌ Ошибка инициализации."); return; }
            userSubs[cid] = sub;
        }
        auto devs = j.contains("devices") ? j["devices"] : nlohmann::json::array();
        if (!message->from) {
            auto from = make_shared<TgBot::User>();
            from->id = cid; from->firstName = "User"; from->isBot = false;
            sub->displayProfile(bot, from, devs, PHOTO_PROFILE);
        } else {
            sub->displayProfile(bot, message->from, devs, PHOTO_PROFILE);
        }
    } catch (const exception& e) { logError("showMesProfile", e.what()); }
}

// ── Статус ───────────────────────────────────────────────────
inline void showMesStatus(TgBot::Bot& bot, long long tgId) {
    auto r = cpr::Get(cpr::Url{API_URL + "/bot/status?tg_id=" + to_string(tgId)},
        cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
    if (r.status_code != 200) {
        bot.getApi().sendMessage(tgId, "❌ Сервер недоступен.");
        return;
    }
    try {
        auto j = nlohmann::json::parse(r.text);
        string rank = j.value("user_rank", "FREE");

        if (rank == "OVERSEER") {
            bool hasPsu = j.contains("psu_watts") && !j["psu_watts"].is_null();
            if (!hasPsu) {
                InlineKeyboardMarkup::Ptr kb(new InlineKeyboardMarkup);
                auto b = make_shared<InlineKeyboardButton>();
                b->text = "⏭ Пропустить (250W)"; b->callbackData = "psu_skip";
                kb->inlineKeyboard.push_back({b});
                bot.getApi().sendMessage(tgId,
                    "⚡ <b>Укажите мощность БП</b>\nПример: <code>/psu 550</code>",
                    nullptr, nullptr, kb, "HTML");
                return;
            }
        }

        shared_ptr<ISubscription> sub;
        {
            lock_guard<mutex> lock(globalMutex);
            sub = SubFactory::create(rank);
            if (!sub) { bot.getApi().sendMessage(tgId, "❌ Ошибка инициализации."); return; }
            userSubs[tgId] = sub;
        }

        nlohmann::json devs = nlohmann::json::array();
        if (j.contains("devices") && j["devices"].is_array()) devs = j["devices"];

        if (devs.empty()) {
            bot.getApi().sendMessage(tgId,
                "📡 <b>Устройства не обнаружены.</b>\n\nНажми /download чтобы установить агента.",
                nullptr, nullptr, nullptr, "HTML");
            return;
        }

        if (devs.size() == 1) {
            auto dev = devs[0];
            string hwid = dev.value("hwid", "");
            bool online = dev.value("online", false);
            bool hasAS = false;
            { lock_guard<mutex> lock(globalMutex);
              hasAS = autoSleepSchedule.count(tgId) && autoSleepSchedule[tgId].first == hwid; }
            auto kb = buildDeviceKeyboard(hwid, online, sub, hasAS);
            bot.getApi().sendMessage(tgId, sub->formatStatus(dev), nullptr, nullptr, kb, "HTML");
            return;
        }

        InlineKeyboardMarkup::Ptr kb(new InlineKeyboardMarkup);
        string lt = "🖥 <b>NETWORK DASHBOARD</b>\n──────────────────────────\n";
        for (auto& dev : devs) {
            bool on = dev.value("online", false);
            string nm = dev.value("name", "PC");
            string hw = dev.value("hwid", "");
            lt += (on ? "🟢 " : "🔴 ") + nm + " [<code>" + hw.substr(0,8) + "...</code>]\n";
            auto b = make_shared<InlineKeyboardButton>();
            b->text = (on ? "⚡️ " : "📁 ") + nm;
            b->callbackData = "refresh_stats:" + hw;
            kb->inlineKeyboard.push_back({b});
        }
        lt += "──────────────────────────\n💎 <b>" + rank + "</b>";
        bot.getApi().sendMessage(tgId, lt, nullptr, nullptr, kb, "HTML");
    } catch (const exception& e) {
        logError("showMesStatus", e.what());
        bot.getApi().sendMessage(tgId, "❌ Ошибка обработки данных.");
    }
}

// ── main ─────────────────────────────────────────────────────
int main() {
    const char* shop_id    = getenv("YOOKASSA_SHOP_ID");
    const char* secret_key = getenv("YOOKASSA_SECRET_KEY");
    if (!shop_id || !secret_key) {
        cerr << "YOOKASSA_SHOP_ID or YOOKASSA_SECRET_KEY not set" << endl;
        return 1;
    }

    const char* te = getenv("NEUROGUARD_BOT_TOKEN");
    if (!te || string(te).size() < 30) {
        cerr << "NEUROGUARD_BOT_TOKEN invalid" << endl;
        return 1;
    }

    Bot bot(te);
    AdminPanel adminPanel(bot, API_URL);
    logInfo("main", "Bot started");

    // ── Регистрация хандлеров ─────────────────────────────
    registerCommandHandlers(bot);
    registerCallbackHandlers(bot);
    registerMessageHandlers(bot);
    adminPanel.registerHandlers();

    // ── Фоновые потоки ────────────────────────────────────
    thread t1(autoSleepThread, ref(bot), ref(running));
    thread t2(subscriptionAlertThread, ref(bot), ref(running));
    t1.detach();
    t2.detach();

    // ── Сигналы ───────────────────────────────────────────
    signal(SIGINT,  [](int){ logInfo("signal", "SIGINT");  running = false; });
    signal(SIGTERM, [](int){ logInfo("signal", "SIGTERM"); running = false; });

    // ── Polling ───────────────────────────────────────────
    logInfo("main", "Polling started");
    int reconnectAttempts = 0;
    const int MAX_RECONNECT = 5;

    while (running) {
        try {
            reconnectAttempts = 0;
            TgLongPoll lp(bot);
            while (running) {
                try {
                    lp.start();
                } catch (const exception& e) {
                    logError("polling", e.what());
                    this_thread::sleep_for(chrono::milliseconds(1000));
                    if (++reconnectAttempts > MAX_RECONNECT) throw;
                }
            }
        } catch (const exception& e) {
            logError("main", e.what());
            if (running)
                this_thread::sleep_for(chrono::milliseconds(3000));
        }
    }

    logInfo("main", "Bot stopped");
    return 0;
}