#pragma once
// ═══════════════════════════════════════════════════════════
//  CommandHandlers.hpp
//  Обработчики команд — /start, /download, /auth, /psu...
// ═══════════════════════════════════════════════════════════

#include <string>
#include <algorithm>
#include <tgbot/tgbot.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "../config/BotConfig.hpp"
#include "../state/BotState.hpp"
#include "../utils/BotHelpers.hpp"
#include "../ui/Keyboards.hpp"
#include "../subscription/SubFactory.hpp"

inline void registerCommandHandlers(TgBot::Bot& bot) {

    bot.getEvents().onCommand("start", [&](TgBot::Message::Ptr m) {
        long long cid = m->chat->id;
        cpr::Get(cpr::Url{API_URL + "/bot/status?tg_id=" + std::to_string(cid)},
                 cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                 cpr::Timeout{5000});
        bot.getApi().sendMessage(cid, TEXT_WELCOME, nullptr, nullptr, makeMainMenu(), "HTML");
    });

    bot.getEvents().onCommand("download", [&](TgBot::Message::Ptr m) {
        long long cid = m->chat->id;
        bot.getApi().sendMessage(cid, TEXT_DOWNLOAD, nullptr, nullptr, makeBackMenu(), "HTML");
        std::thread([&bot, cid]() {
            try {
                bot.getApi().sendDocument(
                    cid,
                    TgBot::InputFile::fromFile("Archive/NeuroGuard.rar", "application/octet-stream"),
                    "", "", nullptr, nullptr, "", false);
            } catch (const std::exception& e) {
                logError("download", e.what());
                try { bot.getApi().sendMessage(cid, "❌ Файл временно недоступен."); } catch (...) {}
            }
        }).detach();
    });

    bot.getEvents().onCommand("subscribe", [&](TgBot::Message::Ptr m) {
        long long cid = m->chat->id;
        if (fileExists(PHOTO_SUB))
            bot.getApi().sendPhoto(cid,
                TgBot::InputFile::fromFile(PHOTO_SUB, "image/png"),
                TEXT_SUB_CAPTION, 0, makeSubKeyboard(), "HTML");
        else
            bot.getApi().sendMessage(cid, TEXT_SUB_CAPTION, nullptr, nullptr, makeSubKeyboard(), "HTML");
    });

    bot.getEvents().onCommand("guide", [&](TgBot::Message::Ptr m) {
        TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);
        auto btnNews = std::make_shared<TgBot::InlineKeyboardButton>();
        btnNews->text = "📢 Новости"; btnNews->url = NEWS_CHANNEL;
        auto btnSupp = std::make_shared<TgBot::InlineKeyboardButton>();
        btnSupp->text = "✉️ Поддержка"; btnSupp->url = "https://t.me/TLEET_BLANT";
        kb->inlineKeyboard.push_back({btnNews, btnSupp});

        // Текст гайда — длинный, оставляем inline
        const std::string guideText =
            "╔══════════════════════════════╗\n"
            "║   📖  ГАЙД  NEUROGUARD       ║\n"
            "╚══════════════════════════════╝\n\n"
            "1️⃣ /download — скачай архив\n"
            "2️⃣ Запусти <code>NeuroGuard_Setup.exe</code> от администратора\n"
            "3️⃣ Скопируй код из окна агента\n"
            "4️⃣ Отправь боту: <code>/auth КОД</code>\n"
            "5️⃣ Введи имя ПК — готово! 🎉\n\n"
            "📊 <code>/status</code> — список устройств\n"
            "💎 <code>/subscribe</code> — тарифы\n"
            "🔐 <code>/setpin</code> — пин-код\n"
            "⚡ <code>/psu 550</code> — мощность БП\n\n"
            "🆓 <b>FREE</b>      — 1 ПК, мониторинг\n"
            "🛡 <b>SENTINEL</b> — 3 ПК, 299₽/мес\n"
            "👑 <b>OVERSEER</b> — 10 ПК, 499₽/мес\n\n"
            "🎁 <b>7 дней OVERSEER бесплатно</b> — /subscribe\n\n"
            "✉️ Поддержка: @TLEET_BLANT";

        bot.getApi().sendMessage(m->chat->id, guideText, nullptr, nullptr, kb, "HTML");
    });

    bot.getEvents().onCommand("setpin", [&](TgBot::Message::Ptr m) {
        {
            std::lock_guard<std::mutex> lock(globalMutex);
            awaitingSetPin.insert(m->chat->id);
        }
        bot.getApi().sendMessage(m->chat->id,
            "🔐 <b>Установка пин-кода</b>\n\n"
            "Введите <b>4 цифры</b>.\n"
            "Пин запрашивается при выключении, сне и скриншоте.\n\n"
            "<i>Удалить пин: /removepin</i>",
            nullptr, nullptr, nullptr, "HTML");
    });

    bot.getEvents().onCommand("removepin", [&](TgBot::Message::Ptr m) {
        long long uid = m->chat->id;
        bool hasPin = false;
        {
            std::lock_guard<std::mutex> lock(globalMutex);
            hasPin = userPins.count(uid) > 0;
        }
        if (!hasPin) {
            bot.getApi().sendMessage(uid, "ℹ️ У вас не установлен пин-код.", nullptr, nullptr, nullptr, "HTML");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(globalMutex);
            awaitingRemovePinConfirm.insert(uid);
        }
        bot.getApi().sendMessage(uid,
            "🔐 <b>Удаление пин-кода</b>\n\nВведите текущий пин для подтверждения:",
            nullptr, nullptr, nullptr, "HTML");
    });

    bot.getEvents().onCommand("psu", [&](TgBot::Message::Ptr m) {
        long long uid = m->chat->id;
        if (m->text.length() <= 5) {
            bot.getApi().sendMessage(uid,
                "⚡ <b>Укажите мощность БП:</b>\n<code>/psu 550</code>",
                nullptr, nullptr, nullptr, "HTML");
            return;
        }
        std::string ws = m->text.substr(5);
        ws.erase(std::remove(ws.begin(), ws.end(), ' '), ws.end());
        int w = 0;
        if (!safeStoi(ws, w) || w < 100 || w > 3000) {
            bot.getApi().sendMessage(uid, "❌ Допустимо 100–3000 Вт. Пример: <code>/psu 550</code>",
                nullptr, nullptr, nullptr, "HTML");
            return;
        }
        auto r = cpr::Post(cpr::Url{API_URL + "/bot/set_psu"},
            cpr::Parameters{{"tg_id", std::to_string(uid)}, {"watts", std::to_string(w)}},
            cpr::Header{{"ngrok-skip-browser-warning", "true"}},
            cpr::Timeout{5000});
        if (r.status_code == 200)
            bot.getApi().sendMessage(uid,
                "✅ Мощность БП: <b>" + std::to_string(w) + "W</b> сохранена.",
                nullptr, nullptr, nullptr, "HTML");
        else
            bot.getApi().sendMessage(uid, "❌ Ошибка сохранения.", nullptr, nullptr, nullptr, "HTML");
    });

    bot.getEvents().onCommand("auth", [&](TgBot::Message::Ptr m) {
        long long uid = m->chat->id;
        if (m->text.length() <= 6) {
            bot.getApi().sendMessage(uid, "⚠️ Формат: <code>/auth КОД</code>", nullptr, nullptr, nullptr, "HTML");
            return;
        }
        std::string code = m->text.substr(6);
        code.erase(std::remove(code.begin(), code.end(), ' '), code.end());
        if (code.length() < 6 || code.length() > 32 ||
            !std::all_of(code.begin(), code.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '-' || c == '_'; })) {
            bot.getApi().sendMessage(uid, "❌ Некорректный код.", nullptr, nullptr, nullptr, "HTML");
            return;
        }
        auto r = cpr::Post(cpr::Url{API_URL + "/auth/verify"},
            cpr::Parameters{{"code", code}, {"tg_id", std::to_string(uid)}},
            cpr::Header{{"ngrok-skip-browser-warning", "true"}},
            cpr::Timeout{5000});
        std::string det;
        try {
            auto ej = nlohmann::json::parse(r.text);
            if (ej.contains("detail") && ej["detail"].is_string())
                det = ej["detail"].get<std::string>();
        } catch (...) {}

        if (r.status_code == 200) {
            std::lock_guard<std::mutex> lock(globalMutex);
            awaitingName[uid] = true;
            bot.getApi().sendMessage(uid,
                "✅ <b>Устройство привязано!</b>\nВведите название ПК:",
                nullptr, nullptr, nullptr, "HTML");
        } else if (r.status_code == 410 || det == "code_expired")
            bot.getApi().sendMessage(uid, "⌛ Код истёк. Перезапустите агент.", nullptr, nullptr, nullptr, "HTML");
        else if (r.status_code == 429 || det == "too_many_attempts")
            bot.getApi().sendMessage(uid, "🚫 Слишком много попыток.", nullptr, nullptr, nullptr, "HTML");
        else if (r.status_code == 403 || det == "limit_reached")
            bot.getApi().sendMessage(uid, "🚫 Лимит устройств. Купите подписку (/subscribe).", nullptr, nullptr, nullptr, "HTML");
        else if (r.status_code == 404 || det == "invalid_code")
            bot.getApi().sendMessage(uid, "❌ Код не найден.", nullptr, nullptr, nullptr, "HTML");
        else
            bot.getApi().sendMessage(uid, "❌ Ошибка (" + std::to_string(r.status_code) + ")", nullptr, nullptr, nullptr, "HTML");
    });
}