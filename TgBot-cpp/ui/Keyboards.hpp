#pragma once
// ═══════════════════════════════════════════════════════════
//  Keyboards.hpp
//  Клавиатуры — главное меню, подписки, устройства
// ═══════════════════════════════════════════════════════════

#include <memory>
#include <string>
#include <tgbot/tgbot.h>
#include "../subscription/ISubscription.hpp"
#include "../utils/BotHelpers.hpp"

// ── Главное меню ─────────────────────────────────────────────
inline TgBot::ReplyKeyboardMarkup::Ptr makeMainMenu() {
    TgBot::ReplyKeyboardMarkup::Ptr kb(new TgBot::ReplyKeyboardMarkup);
    kb->resizeKeyboard = true;
    kb->keyboard = {
        {makeButton("📊 Статус"), makeButton("ℹ️ FAQ")},
        {makeButton("🆘 Поддержка"), makeButton("💎 Подписка")},
        {makeButton("👤 Профиль")}
    };
    return kb;
}

// ── Назад ────────────────────────────────────────────────────
inline TgBot::ReplyKeyboardMarkup::Ptr makeBackMenu() {
    TgBot::ReplyKeyboardMarkup::Ptr kb(new TgBot::ReplyKeyboardMarkup);
    kb->resizeKeyboard = true;
    kb->keyboard = {{makeButton("◀️ Вернуться в главное меню")}};
    return kb;
}

// ── Подписки ─────────────────────────────────────────────────
inline TgBot::InlineKeyboardMarkup::Ptr makeSubKeyboard() {
    TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);
    auto bt = std::make_shared<TgBot::InlineKeyboardButton>();
    bt->text = "🎁 Пробный 7 дней (OVERSEER)";
    bt->callbackData = "trial_overseer";
    auto bs = std::make_shared<TgBot::InlineKeyboardButton>();
    bs->text = "🛡 SENTINEL — 299₽/мес";
    bs->callbackData = "buy_sentinel";
    auto bo = std::make_shared<TgBot::InlineKeyboardButton>();
    bo->text = "👑 OVERSEER — 499₽/мес";
    bo->callbackData = "buy_overseer";
    kb->inlineKeyboard = {{bt}, {bs}, {bo}};
    return kb;
}

// ── Клавиатура устройства ────────────────────────────────────
inline TgBot::InlineKeyboardMarkup::Ptr buildDeviceKeyboard(
    const std::string& hwid, bool online,
    const std::shared_ptr<ISubscription>& sub,
    bool hasAS = false)
{
    TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);

    auto bRef = std::make_shared<TgBot::InlineKeyboardButton>();
    bRef->text = "🔄 Обновить";
    bRef->callbackData = "refresh_stats:" + hwid;
    auto bSet = std::make_shared<TgBot::InlineKeyboardButton>();
    bSet->text = "⚙️ Опции";
    bSet->callbackData = "setup_" + hwid;
    kb->inlineKeyboard.push_back({bRef, bSet});

    if (online && sub->canControlPower()) {
        auto bOff = std::make_shared<TgBot::InlineKeyboardButton>();
        bOff->text = "🔌 Выключить";
        bOff->callbackData = "off_" + hwid;
        auto bRev = std::make_shared<TgBot::InlineKeyboardButton>();
        bRev->text = "↩ Отменить выкл";
        bRev->callbackData = "revokecmd_" + hwid;
        kb->inlineKeyboard.push_back({bOff, bRev});
    }

    if (online && sub->canTakeScreenshots()) {
        auto bSlp = std::make_shared<TgBot::InlineKeyboardButton>();
        bSlp->text = "🌙 Сон";
        bSlp->callbackData = "sleep_" + hwid;
        auto bPrc = std::make_shared<TgBot::InlineKeyboardButton>();
        bPrc->text = "📊 Процессы";
        bPrc->callbackData = "get_procs_" + hwid;
        kb->inlineKeyboard.push_back({bSlp, bPrc});

        auto bScr = std::make_shared<TgBot::InlineKeyboardButton>();
        bScr->text = "📸 Скриншот";
        bScr->callbackData = "scr_" + hwid;
        kb->inlineKeyboard.push_back({bScr});

        auto bAS = std::make_shared<TgBot::InlineKeyboardButton>();
        if (hasAS) {
            bAS->text = "⏰ Авто-сон: ВКЛ";
            bAS->callbackData = "autosleep_off_" + hwid;
        } else {
            bAS->text = "⏰ Авто-сон";
            bAS->callbackData = "autosleep_set_" + hwid;
        }
        kb->inlineKeyboard.push_back({bAS});
    }

    auto bH = std::make_shared<TgBot::InlineKeyboardButton>();
    bH->text = "🏥 Состояние ПК";
    bH->callbackData = "health_" + hwid;
    kb->inlineKeyboard.push_back({bH});

    auto bBk = std::make_shared<TgBot::InlineKeyboardButton>();
    bBk->text = "◀️ Назад к списку";
    bBk->callbackData = "back_to_status";
    kb->inlineKeyboard.push_back({bBk});

    return kb;
}