#pragma once
// ═══════════════════════════════════════════════════════════
//  MessageHandlers.hpp
//  onAnyMessage — кнопки меню, пин, авто-сон, имя устройства
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

// forward declarations
inline void showMesStatus(TgBot::Bot& bot, long long tgId);
inline void showMesProfile(TgBot::Bot& bot, TgBot::Message::Ptr message);

inline void registerMessageHandlers(TgBot::Bot& bot) {
    bot.getEvents().onAnyMessage([&](TgBot::Message::Ptr m) {
        long long tgId = m->chat->id;
        std::string text = m->text;
        if (text.empty() || text[0] == '/') return;
        if (!checkRateLimit(tgId, 800)) return;

        {
            std::lock_guard<std::mutex> lock(globalMutex);

            if (awaitingSetPin.count(tgId)) {
                awaitingSetPin.erase(tgId);
                if (text.size() == 4 && std::all_of(text.begin(), text.end(), ::isdigit)) {
                    userPins[tgId] = text;
                    bot.getApi().sendMessage(tgId,
                        "✅ <b>Пин-код установлен!</b>\n\nУдалить: /removepin",
                        nullptr, nullptr, nullptr, "HTML");
                } else {
                    bot.getApi().sendMessage(tgId,
                        "❌ Введите ровно <b>4 цифры</b>. Попробуйте /setpin ещё раз.",
                        nullptr, nullptr, nullptr, "HTML");
                }
                return;
            }

            if (awaitingRemovePinConfirm.count(tgId)) {
                awaitingRemovePinConfirm.erase(tgId);
                if (userPins.count(tgId) && userPins[tgId] == text) {
                    userPins.erase(tgId);
                    bot.getApi().sendMessage(tgId, "🔓 <b>Пин-код удалён.</b>", nullptr, nullptr, nullptr, "HTML");
                } else {
                    bot.getApi().sendMessage(tgId, "❌ Неверный пин.", nullptr, nullptr, nullptr, "HTML");
                }
                return;
            }

            if (awaitingPin.count(tgId)) {
                try {
                    auto [hwid, cmd] = awaitingPin[tgId];
                    awaitingPin.erase(tgId);
                    if (userPins.count(tgId) && userPins[tgId] == text) {
                        auto r = cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                            cpr::Parameters{{"hwid", hwid}, {"command", cmd}, {"tg_id", std::to_string(tgId)}},
                            cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                            cpr::Timeout{5000});
                        bot.getApi().sendMessage(tgId,
                            r.status_code == 200
                                ? "✅ Команда <b>" + cmd + "</b> отправлена."
                                : "❌ Ошибка отправки команды.",
                            nullptr, nullptr, nullptr, "HTML");
                    } else {
                        bot.getApi().sendMessage(tgId, "❌ Неверный пин. Отменено.", nullptr, nullptr, nullptr, "HTML");
                    }
                } catch (...) { awaitingPin.erase(tgId); }
                return;
            }

            if (awaitingAutoSleepTime.count(tgId)) {
                std::string hwid = awaitingAutoSleepTime[tgId];
                awaitingAutoSleepTime.erase(tgId);
                auto [h, mn] = parseTime(text);
                if (h >= 0) {
                    autoSleepSchedule[tgId] = {hwid, text};
                    bot.getApi().sendMessage(tgId,
                        "✅ <b>Авто-сон настроен: " + text + " МСК</b>\n\n"
                        "Отключить: кнопка <b>⏰ Авто-сон: ВКЛ</b> в статусе.",
                        nullptr, nullptr, nullptr, "HTML");
                } else {
                    bot.getApi().sendMessage(tgId,
                        "❌ Неверный формат. Введите время как <code>23:30</code>",
                        nullptr, nullptr, nullptr, "HTML");
                }
                return;
            }
        }

        if (text == "◀️ Вернуться в главное меню") {
            sendPhoto(bot, tgId, PHOTO_MAIN, TEXT_MAIN_CAPTION, makeMainMenu());
            return;
        }

        {
            std::lock_guard<std::mutex> lock(globalMutex);

            if (awaitingName.count(tgId)) {
                awaitingName.erase(tgId);
                if (text.empty() || text.length() > 64) {
                    bot.getApi().sendMessage(tgId, "❌ Имя должно быть 1-64 символа.", nullptr, nullptr, nullptr, "HTML");
                    return;
                }
                std::string san = text;
                san.erase(std::remove_if(san.begin(), san.end(),
                    [](unsigned char c){ return c < 32 || c == '"' || c == '\''; }), san.end());
                auto r = cpr::Post(cpr::Url{API_URL + "/auth/update_name"},
                    cpr::Parameters{{"tg_id", std::to_string(tgId)}, {"name", san}},
                    cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                    cpr::Timeout{5000});
                bot.getApi().sendMessage(tgId,
                    r.status_code == 200
                        ? "✅ ПК назван: <b>" + san + "</b>\n\nПроверьте 📊 Статус."
                        : "❌ Ошибка сохранения.",
                    nullptr, nullptr, makeMainMenu(), "HTML");
                return;
            }

            if (renamingDeviceId.count(tgId)) {
                std::string hwid = renamingDeviceId[tgId];
                renamingDeviceId.erase(tgId);
                if (text.empty() || text.length() > 64) {
                    bot.getApi().sendMessage(tgId, "❌ Имя должно быть 1-64 символа.", nullptr, nullptr, nullptr, "HTML");
                    return;
                }
                std::string san = text;
                san.erase(std::remove_if(san.begin(), san.end(),
                    [](unsigned char c){ return c < 32 || c == '"' || c == '\''; }), san.end());
                auto r = cpr::Post(cpr::Url{API_URL + "/auth/update_name"},
                    cpr::Parameters{{"tg_id", std::to_string(tgId)}, {"name", san}, {"hwid", hwid}},
                    cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                    cpr::Timeout{5000});
                bot.getApi().sendMessage(tgId,
                    r.status_code == 200
                        ? "✅ Имя изменено: <b>" + san + "</b>"
                        : "❌ Ошибка сохранения.",
                    nullptr, nullptr, makeMainMenu(), "HTML");
                return;
            }
        }

        if      (text == "📊 Статус")    showMesStatus(bot, tgId);
        else if (text == "🆘 Поддержка") sendPhoto(bot, tgId, PHOTO_SUPP, TEXT_SUPPORT, makeBackMenu());
        else if (text == "ℹ️ FAQ")        sendPhoto(bot, tgId, PHOTO_FAQ,  TEXT_ABOUT,   makeBackMenu());
        else if (text == "👤 Профиль")   showMesProfile(bot, m);
        else if (text == "💎 Подписка") {
            if (fileExists(PHOTO_SUB))
                bot.getApi().sendPhoto(tgId,
                    TgBot::InputFile::fromFile(PHOTO_SUB, "image/png"),
                    TEXT_SUB_CAPTION, 0, makeSubKeyboard(), "HTML");
            else
                bot.getApi().sendMessage(tgId, TEXT_SUB_CAPTION, nullptr, nullptr, makeSubKeyboard(), "HTML");
        }
    });
}