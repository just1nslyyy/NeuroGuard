#pragma once
// ═══════════════════════════════════════════════════════════
//  CallbackHandlers.hpp
//  Обработчики callback query
// ═══════════════════════════════════════════════════════════

#include <string>
#include <sstream>
#include <algorithm>
#include <tgbot/tgbot.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "../config/BotConfig.hpp"
#include "../state/BotState.hpp"
#include "../utils/BotHelpers.hpp"
#include "../ui/Keyboards.hpp"
#include "../subscription/SubFactory.hpp"

inline void showMesStatus(TgBot::Bot& bot, long long tgId);
inline void showMesProfile(TgBot::Bot& bot, TgBot::Message::Ptr message);

inline void registerCallbackHandlers(TgBot::Bot& bot) {
    bot.getEvents().onCallbackQuery([&](TgBot::CallbackQuery::Ptr q) {
        try { bot.getApi().answerCallbackQuery(q->id); } catch (...) {}

        long long tgId = q->message->chat->id;
        if (!checkRateLimit(tgId)) {
            try { bot.getApi().answerCallbackQuery(q->id, "⏳ Подождите...", false, "", 1); } catch (...) {}
            return;
        }
        const std::string& d = q->data;
        int mid = q->message->messageId;

        // ── refresh_stats ────────────────────────────────────
        if (d.rfind("refresh_stats:", 0) == 0) {
            std::string hwid = d.substr(14);
            auto r = cpr::Get(cpr::Url{API_URL + "/bot/status?tg_id=" + std::to_string(tgId)},
                cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
            if (r.status_code != 200) return;
            try {
                auto j = nlohmann::json::parse(r.text);
                auto sub = SubFactory::create(j.value("user_rank", "FREE"));
                for (auto& dev : j["devices"]) {
                    if (dev.value("hwid", "") != hwid) continue;
                    bool hasAS = false;
                    { std::lock_guard<std::mutex> lk(globalMutex);
                      hasAS = autoSleepSchedule.count(tgId) && autoSleepSchedule[tgId].first == hwid; }
                    auto kb = buildDeviceKeyboard(hwid, dev.value("online", false), sub, hasAS);
                    try { bot.getApi().editMessageText(sub->formatStatus(dev), tgId, mid, "", "HTML", nullptr, kb); }
                    catch (const std::exception& e) { logError("refresh_stats", e.what()); }
                    break;
                }
            } catch (const std::exception& e) { logError("refresh_stats", e.what()); }

        // ── back_to_status ───────────────────────────────────
        } else if (d == "back_to_status") {
            showMesStatus(bot, tgId);

        // ── back_to_profile ──────────────────────────────────
        } else if (d == "back_to_profile") {
            auto fm = std::make_shared<TgBot::Message>();
            fm->chat = q->message->chat;
            fm->from = std::make_shared<TgBot::User>();
            fm->from->id = q->message->chat->id;
            fm->from->firstName = "User";
            fm->from->isBot = false;
            showMesProfile(bot, fm);

        // ── setup_ ───────────────────────────────────────────
        } else if (d.rfind("setup_", 0) == 0) {
            std::string hwid = d.substr(6);
            if (hwid.empty() || hwid.length() > 32) return;
            TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);
            auto br = std::make_shared<TgBot::InlineKeyboardButton>();
            br->text = "✏️ Изменить имя"; br->callbackData = "ren_" + hwid;
            auto bd = std::make_shared<TgBot::InlineKeyboardButton>();
            bd->text = "🗑 Удалить"; bd->callbackData = "del_" + hwid;
            auto bb = std::make_shared<TgBot::InlineKeyboardButton>();
            bb->text = "◀️ Назад"; bb->callbackData = "back_to_profile";
            kb->inlineKeyboard = {{br, bd}, {bb}};
            try { bot.getApi().editMessageText(
                "⚙️ <b>Управление устройством</b>\n<code>" + hwid + "</code>",
                tgId, mid, "", "HTML", nullptr, kb); } catch (...) {}

        // ── del_ ─────────────────────────────────────────────
        } else if (d.rfind("del_", 0) == 0) {
            std::string hwid = d.substr(4);
            if (hwid.empty() || hwid.length() > 32) return;
            auto r = cpr::Post(cpr::Url{API_URL + "/auth/delete_device"},
                cpr::Parameters{{"tg_id", std::to_string(tgId)}, {"hwid", hwid}},
                cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
            if (r.status_code == 200)
                try { bot.getApi().editMessageText("✅ Устройство отвязано.", tgId, mid, "", "HTML"); } catch (...) {}
            else
                try { bot.getApi().answerCallbackQuery(q->id, "❌ Ошибка удаления", false, "", 3); } catch (...) {}

        // ── ren_ ─────────────────────────────────────────────
        } else if (d.rfind("ren_", 0) == 0) {
            std::string hwid = d.substr(4);
            if (hwid.empty() || hwid.length() > 32) return;
            std::lock_guard<std::mutex> lk(globalMutex);
            renamingDeviceId[tgId] = hwid;
            bot.getApi().sendMessage(tgId,
                "📝 Введите новое имя для <code>" + hwid.substr(0,8) + "...</code>:",
                nullptr, nullptr, nullptr, "HTML");

        // ── off_ ─────────────────────────────────────────────
        } else if (d.rfind("off_", 0) == 0) {
            std::string hwid = d.substr(4);
            bool hasPin = false;
            { std::lock_guard<std::mutex> lk(globalMutex);
              hasPin = userPins.count(tgId);
              if (hasPin) awaitingPin[tgId] = {hwid, "shutdown"}; }
            if (hasPin)
                bot.getApi().sendMessage(tgId, "🔐 Введите пин для выключения:", nullptr, nullptr, nullptr, "HTML");
            else {
                cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                    cpr::Parameters{{"hwid", hwid}, {"command", "shutdown"}, {"tg_id", std::to_string(tgId)}},
                    cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
                bot.getApi().sendMessage(tgId, "🔌 Команда на выключение отправлена.");
            }

        // ── revokecmd_ ───────────────────────────────────────
        } else if (d.rfind("revokecmd_", 0) == 0) {
            std::string hwid = d.substr(10);
            cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                cpr::Parameters{{"hwid", hwid}, {"command", "cancel_shutdown"}, {"tg_id", std::to_string(tgId)}},
                cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
            try { bot.getApi().editMessageText("<b>✅ Выключение отозвано</b>", tgId, mid, "", "HTML"); } catch (...) {}

        // ── sleep_ ───────────────────────────────────────────
        } else if (d.rfind("sleep_", 0) == 0) {
            std::string hwid = d.substr(6);
            bool hasPin = false;
            { std::lock_guard<std::mutex> lk(globalMutex);
              hasPin = userPins.count(tgId);
              if (hasPin) awaitingPin[tgId] = {hwid, "sleep"}; }
            if (hasPin)
                bot.getApi().sendMessage(tgId, "🔐 Введите пин для сна:", nullptr, nullptr, nullptr, "HTML");
            else {
                cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                    cpr::Parameters{{"hwid", hwid}, {"command", "sleep"}, {"tg_id", std::to_string(tgId)}},
                    cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
                try { bot.getApi().answerCallbackQuery(q->id, "🌙 Сон отправлен", true); } catch (...) {}
            }

        // ── cancel_sleep_ ────────────────────────────────────
        } else if (d.rfind("cancel_sleep_", 0) == 0) {
            std::string hwid = d.substr(13);
            { std::lock_guard<std::mutex> lk(globalMutex);
              auto it = autoSleepSchedule.find(tgId);
              if (it != autoSleepSchedule.end()) { hwid = it->second.first; autoSleepSchedule.erase(it); } }
            if (!hwid.empty()) {
                auto r = cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                    cpr::Parameters{{"hwid", hwid}, {"command", "cancel_shutdown"}, {"tg_id", std::to_string(tgId)}},
                    cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
                if (r.status_code == 200)
                    try { bot.getApi().editMessageText("✅ Авто-сон отменён.", tgId, mid, "", "HTML"); } catch (...) {}
            }

        // ── autosleep_set_ ───────────────────────────────────
        } else if (d.rfind("autosleep_set_", 0) == 0) {
            std::lock_guard<std::mutex> lk(globalMutex);
            awaitingAutoSleepTime[tgId] = d.substr(14);
            bot.getApi().sendMessage(tgId,
                "⏰ <b>Авто-сон</b>\n\nВведите время МСК: <code>ЧЧ:ММ</code>\nПример: <code>23:30</code>",
                nullptr, nullptr, nullptr, "HTML");

        // ── autosleep_off_ ───────────────────────────────────
        } else if (d.rfind("autosleep_off_", 0) == 0) {
            { std::lock_guard<std::mutex> lk(globalMutex); autoSleepSchedule.erase(tgId); }
            try { bot.getApi().editMessageText("✅ Авто-сон отключён.", tgId, mid, "", "HTML"); } catch (...) {}

        // ── scr_ ─────────────────────────────────────────────
        } else if (d.rfind("scr_", 0) == 0) {
            std::string hwid = d.substr(4);
            bool hasPin = false;
            { std::lock_guard<std::mutex> lk(globalMutex);
              hasPin = userPins.count(tgId);
              if (hasPin) awaitingPin[tgId] = {hwid, "screenshot"}; }
            if (hasPin)
                bot.getApi().sendMessage(tgId, "🔐 Введите пин для скриншота:", nullptr, nullptr, nullptr, "HTML");
            else {
                cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                    cpr::Parameters{{"hwid", hwid}, {"command", "screenshot"}, {"tg_id", std::to_string(tgId)}},
                    cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
                try { bot.getApi().answerCallbackQuery(q->id, "📸 Скриншот отправлен", true); } catch (...) {}
            }

        // ── get_procs_ ───────────────────────────────────────
        } else if (d.rfind("get_procs_", 0) == 0) {
            std::string hwid = d.substr(10);
            if (hwid.empty() || hwid.length() > 32) return;
            auto r = cpr::Get(cpr::Url{API_URL + "/bot/status?tg_id=" + std::to_string(tgId)},
                cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
            if (r.status_code != 200) return;
            try {
                auto j = nlohmann::json::parse(r.text);
                if (j.value("user_rank", "FREE") != "OVERSEER") {
                    bot.getApi().sendMessage(tgId, "❌ Только для OVERSEER.", nullptr, nullptr, nullptr, "HTML");
                    return;
                }
                for (auto& dev : j["devices"]) {
                    if (dev.value("hwid", "") != hwid) continue;
                    if (!dev.contains("top_processes") || dev["top_processes"].empty()) {
                        bot.getApi().sendMessage(tgId, "⏳ Данные ещё не поступили.", nullptr, nullptr, nullptr, "HTML");
                        break;
                    }
                    std::string msg = "📊 <b>Топ процессов</b>\n──────────────────────────\n";
                    TgBot::InlineKeyboardMarkup::Ptr pkb(new TgBot::InlineKeyboardMarkup);
                    int cnt = 0;
                    for (auto& p : dev["top_processes"]) {
                        if (cnt++ > 20) break;
                        std::string pn = p.value("name", "?");
                        int pid        = p.value("pid", 0);
                        float pc       = p.value("cpu", 0.0f);
                        float pr       = p.value("ram", 0.0f);
                        msg += "🔹 <b>" + pn + "</b>\n";
                        msg += "└ 💾 <code>" + std::to_string((int)pr) + "MB</code>"
                               " | ⚡ <code>" + toFixed(pc, 1) + "%</code>\n";
                        auto bk = std::make_shared<TgBot::InlineKeyboardButton>();
                        bk->text = "🛑 " + (pn.length() > 15 ? pn.substr(0,12) + "..." : pn);
                        bk->callbackData = "kill_p:" + hwid + ":" + std::to_string(pid);
                        pkb->inlineKeyboard.push_back({bk});
                    }
                    auto bk = std::make_shared<TgBot::InlineKeyboardButton>();
                    bk->text = "◀️ Назад"; bk->callbackData = "refresh_stats:" + hwid;
                    pkb->inlineKeyboard.push_back({bk});
                    if (msg.size() > 4000) msg = msg.substr(0, 3997) + "...";
                    bot.getApi().sendMessage(tgId, msg, nullptr, nullptr, pkb, "HTML");
                    break;
                }
            } catch (const std::exception& e) { logError("get_procs", e.what()); }

        // ── kill_p ───────────────────────────────────────────
        } else if (d.rfind("kill_p:", 0) == 0) {
            std::stringstream ss(d.substr(7));
            std::string hwid, pid;
            std::getline(ss, hwid, ':');
            std::getline(ss, pid, ':');
            if (hwid.empty() || pid.empty() || hwid.length() > 32 || pid.length() > 10 ||
                !std::all_of(pid.begin(), pid.end(), ::isdigit)) return;
            int pidVal = 0;
            if (!safeStoi(pid, pidVal) || pidVal < 0) return;
            auto r = cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                cpr::Parameters{{"hwid", hwid}, {"command", "kill_" + pid}, {"tg_id", std::to_string(tgId)}},
                cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
            if (r.status_code == 200)
                try { bot.getApi().answerCallbackQuery(q->id, "✅ Kill PID " + pid); } catch (...) {}

        // ── health_ ──────────────────────────────────────────
        } else if (d.rfind("health_", 0) == 0) {
            auto r = cpr::Get(cpr::Url{API_URL + "/bot/status?tg_id=" + std::to_string(tgId)},
                cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
            if (r.status_code != 200) return;
            try {
                auto j = nlohmann::json::parse(r.text);
                std::string rank = j.value("user_rank", "FREE");
                if (rank == "FREE") {
                    bot.getApi().sendMessage(tgId,
                        "🔒 <b>Состояние ПК</b>\n\nДоступно только для подписчиков.\n\n👉 /subscribe",
                        nullptr, nullptr, nullptr, "HTML");
                    return;
                }
                std::string hwid;
                if (d.rfind("health_select:", 0) == 0) hwid = d.substr(14);
                else hwid = d.substr(7);
                auto devs = j.contains("devices") ? j["devices"] : nlohmann::json::array();
                if (devs.empty()) { bot.getApi().sendMessage(tgId, "📡 Устройства не найдены."); return; }
                if (hwid.empty()) {
                    if (devs.size() == 1) hwid = devs[0].value("hwid", "");
                    else {
                        TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);
                        for (auto& dev : devs) {
                            auto b = std::make_shared<TgBot::InlineKeyboardButton>();
                            b->text = (dev.value("online",false) ? "🟢 " : "🔴 ") + dev.value("name","PC");
                            b->callbackData = "health_select:" + dev.value("hwid","");
                            kb->inlineKeyboard.push_back({b});
                        }
                        bot.getApi().sendMessage(tgId, "🏥 <b>Выберите устройство:</b>", nullptr, nullptr, kb, "HTML");
                        return;
                    }
                }
                if (hwid.empty()) return;
                auto hr = cpr::Get(
                    cpr::Url{API_URL + "/bot/status/" + hwid + "?tg_id=" + std::to_string(tgId)},
                    cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
                if (hr.status_code != 200) { bot.getApi().sendMessage(tgId, "❌ Ошибка получения данных."); return; }
                auto hj = nlohmann::json::parse(hr.text);
                if (hj.value("status","") == "no_data") {
                    bot.getApi().sendMessage(tgId,
                        "⏳ <b>Данных пока нет.</b>\nЗайдите через несколько часов.",
                        nullptr, nullptr, nullptr, "HTML");
                    return;
                }
                if (hj.value("status","") == "ready") {
                    float cta = hj.value("cpu_temp_avg", 0.0f);
                    float ctm = hj.value("cpu_temp_max", 0.0f);
                    float cla = hj.value("cpu_load_avg", 0.0f);
                    float rla = hj.value("ram_load_avg", 0.0f);
                    std::string ts = ctm>85 ? "🔴 КРИТИЧНО" : ctm>75 ? "🟡 ВНИМАНИЕ" : "🟢 НОРМА";
                    std::string vd = ctm>85 ? "⚠️ Требуется чистка/замена термопасты"
                                   : ctm>75 ? "🔧 Проверьте охлаждение"
                                            : "✅ ПК работает в штатном режиме";
                    std::stringstream ss;
                    ss << "🏥 <b>ДИАГНОСТИКА — " << hj.value("agent_name","Device") << "</b>\n";
                    ss << "━━━━━━━━━━━━━━━━━━━━━━━━\n\n";
                    ss << "🖥 <b>CPU:</b> <code>" << hj.value("cpu_name","N/A") << "</code>\n";
                    ss << "🌡 Средняя: <code>" << toFixed(cta,1) << "°C</code> | Пиковая: <code>" << toFixed(ctm,1) << "°C</code> " << ts << "\n";
                    ss << "📊 Нагрузка: <code>" << toFixed(cla,1) << "%</code> CPU | <code>" << toFixed(rla,1) << "%</code> RAM\n\n";
                    if (hj.contains("disks") && !hj["disks"].empty()) {
                        ss << "💾 <b>Диски:</b>\n";
                        for (auto& dd : hj["disks"]) {
                            ss << "├ <b>" << dd.value("name","?") << ":</b> <code>"
                               << toFixed(dd.value("used",0.0f),1) << "/" << toFixed(dd.value("total",0.0f),1)
                               << " GB (" << (int)dd.value("percent",0.0f) << "%)</code>\n";
                        }
                        ss << "\n";
                    }
                    ss << "⚡ <b>Энергопотребление:</b>\n";
                    if (hj.contains("estimated_kwh") && !hj["estimated_kwh"].is_null())
                        ss << "├ Потреблено: <code>" << toFixed(hj.value("estimated_kwh",0.0f),2) << " кВт/ч</code>\n"
                           << "└ Стоимость: <code>~" << toFixed(hj.value("estimated_cost",0.0f),1) << " ₽</code>\n\n";
                    else
                        ss << "└ не определено — <code>/psu [ватты]</code>\n\n";
                    ss << "━━━━━━━━━━━━━━━━━━━━━━━━\n";
                    ss << "🔮 <b>Итог:</b> " << vd;
                    std::string msgStr = ss.str();
                    if (msgStr.size() > 4000) msgStr = msgStr.substr(0,3997) + "...";
                    TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);
                    auto brf = std::make_shared<TgBot::InlineKeyboardButton>();
                    brf->text = "🔄 Обновить"; brf->callbackData = "health_select:" + hwid;
                    kb->inlineKeyboard.push_back({brf});
                    if (rank == "OVERSEER") {
                        auto bc = std::make_shared<TgBot::InlineKeyboardButton>();
                        bc->text = "🗑 Очистить статистику"; bc->callbackData = "clear_stats_" + hwid;
                        kb->inlineKeyboard.push_back({bc});
                    }
                    bot.getApi().sendMessage(tgId, msgStr, nullptr, nullptr, kb, "HTML");
                }
            } catch (const std::exception& e) { logError("health", e.what()); }

        // ── buy_sentinel / buy_overseer ──────────────────────
        } else if (d == "buy_sentinel" || d == "buy_overseer") {
            std::string rank = (d == "buy_sentinel") ? "SENTINEL" : "OVERSEER";
            auto r = cpr::Post(cpr::Url{API_URL + "/create_payment"},
                cpr::Parameters{{"tg_id", std::to_string(tgId)}, {"rank", rank}},
                cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
            if (r.status_code == 200) {
                try {
                    auto j = nlohmann::json::parse(r.text);
                    std::string pay_url    = j["payment_url"];
                    std::string payment_id = j["payment_id"];
                    { std::lock_guard<std::mutex> lk(globalMutex);
                      awaitingPaymentId[tgId]   = payment_id;
                      awaitingPaymentRank[tgId] = rank; }
                    TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);
                    auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                    btn->text = "💳 Оплатить картой"; btn->url = pay_url;
                    auto btnCheck = std::make_shared<TgBot::InlineKeyboardButton>();
                    btnCheck->text = "✅ Я оплатил"; btnCheck->callbackData = "check_payment";
                    kb->inlineKeyboard = {{btn}, {btnCheck}};
                    bot.getApi().sendMessage(tgId,
                        "💳 <b>Оплата " + rank + "</b>\n\nПерейди по ссылке, оплати,\nзатем нажми «✅ Я оплатил»",
                        nullptr, nullptr, kb, "HTML");
                } catch (const std::exception& e) {
                    logError("buy_payment", e.what());
                    bot.getApi().sendMessage(tgId, "❌ Ошибка создания платежа.");
                }
            } else {
                bot.getApi().sendMessage(tgId, "❌ Ошибка создания платежа. Попробуйте позже.");
            }

        // ── trial_overseer ───────────────────────────────────
        } else if (d == "trial_overseer") {
            auto r = cpr::Post(cpr::Url{API_URL + "/bot/activate_trial"},
                cpr::Parameters{{"tg_id", std::to_string(tgId)}, {"rank", "OVERSEER"}, {"days", "7"}},
                cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
            if (r.status_code == 200)
                bot.getApi().sendMessage(tgId,
                    "🎁 <b>Пробный OVERSEER на 7 дней активирован!</b> 👑\n\n"
                    "• 📸 Скриншоты\n• 📊 Процессы\n• ⏰ Авто-сон\n• 10 устройств\n\n"
                    "Привяжи ПК: /download", nullptr, nullptr, nullptr, "HTML");
            else if (r.status_code == 409)
                bot.getApi().sendMessage(tgId, "❌ Пробный период уже был использован.", nullptr, nullptr, nullptr, "HTML");
            else
                bot.getApi().sendMessage(tgId, "❌ Ошибка активации.", nullptr, nullptr, nullptr, "HTML");

        // ── check_payment ────────────────────────────────────
        } else if (d == "check_payment") {
            std::string payment_id, rank;
            { std::lock_guard<std::mutex> lk(globalMutex);
              auto it1 = awaitingPaymentId.find(tgId);
              auto it2 = awaitingPaymentRank.find(tgId);
              if (it1 == awaitingPaymentId.end() || it2 == awaitingPaymentRank.end()) {
                  bot.getApi().sendMessage(tgId, "❌ Нет активного платежа.");
                  return;
              }
              payment_id = it1->second; rank = it2->second; }
            const char* shop_id_env    = std::getenv("YOOKASSA_SHOP_ID");
            const char* secret_key_env = std::getenv("YOOKASSA_SECRET_KEY");
            if (!shop_id_env || !secret_key_env) {
                bot.getApi().sendMessage(tgId, "❌ Ошибка конфигурации сервера."); return;
            }
            std::string auth = base64Encode(std::string(shop_id_env) + ":" + std::string(secret_key_env));
            auto r = cpr::Get(cpr::Url{"https://api.yookassa.ru/v3/payments/" + payment_id},
                cpr::Header{{"Authorization", "Basic " + auth}, {"Content-Type", "application/json"}},
                cpr::Timeout{10000});
            if (r.status_code == 200) {
                try {
                    auto j = nlohmann::json::parse(r.text);
                    std::string status = j.value("status", "");
                    if (status == "succeeded") {
                        const char* api_token = std::getenv("NEUROGUARD_INTERNAL_API_TOKEN");
                        if (!api_token) { bot.getApi().sendMessage(tgId, "❌ Ошибка: нет internal token."); return; }
                        auto upd = cpr::Post(cpr::Url{API_URL + "/bot/upgrade"},
                            cpr::Payload{{"tg_id", std::to_string(tgId)}, {"rank", rank},
                                         {"days", "30"}, {"api_token", std::string(api_token)}},
                            cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
                        if (upd.status_code == 200) {
                            { std::lock_guard<std::mutex> lk(globalMutex);
                              awaitingPaymentId.erase(tgId); awaitingPaymentRank.erase(tgId); }
                            std::string emoji = (rank == "OVERSEER") ? "👑" : "🛡";
                            bot.getApi().sendMessage(tgId,
                                emoji + " <b>Подписка " + rank + " активирована!</b>\n\n/download",
                                nullptr, nullptr, nullptr, "HTML");
                        } else {
                            bot.getApi().sendMessage(tgId, "❌ Ошибка активации. Напишите @TLEET_BLANT");
                        }
                    } else if (status == "pending" || status == "waiting_for_capture") {
                        bot.getApi().sendMessage(tgId, "⏳ Платёж обрабатывается. Подождите и попробуйте снова.");
                    } else {
                        bot.getApi().sendMessage(tgId, "❌ Платёж не прошёл (статус: " + status + ").");
                    }
                } catch (const std::exception& e) {
                    logError("check_payment", e.what());
                    bot.getApi().sendMessage(tgId, "❌ Ошибка обработки ответа.");
                }
            } else {
                bot.getApi().sendMessage(tgId, "❌ Ошибка проверки платежа (" + std::to_string(r.status_code) + ").");
            }

        // ── psu_skip ─────────────────────────────────────────
        } else if (d == "psu_skip") {
            cpr::Post(cpr::Url{API_URL + "/bot/set_psu"},
                cpr::Parameters{{"tg_id", std::to_string(tgId)}, {"watts", "250"}},
                cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
            bot.getApi().sendMessage(tgId,
                "✅ Установлена заглушка <b>250W</b>.\nИзменить: <code>/psu [ватты]</code>",
                nullptr, nullptr, nullptr, "HTML");
            showMesStatus(bot, tgId);

        // ── clear_stats_ ─────────────────────────────────────
        } else if (d.rfind("clear_stats_", 0) == 0) {
            std::string hwid = d.substr(12);
            auto r = cpr::Post(cpr::Url{API_URL + "/bot/clear_stats"},
                cpr::Parameters{{"hwid", hwid}, {"tg_id", std::to_string(tgId)}},
                cpr::Header{{"ngrok-skip-browser-warning", "true"}}, cpr::Timeout{5000});
            if (r.status_code == 200)
                try { bot.getApi().editMessageText("🗑 <b>Статистика очищена.</b>", tgId, mid, "", "HTML"); } catch (...) {}

        // ── delete_msg ───────────────────────────────────────
        } else if (d == "delete_msg") {
            try { bot.getApi().deleteMessage(tgId, mid); } catch (...) {}
        }
    });
}