#pragma once
// ═══════════════════════════════════════════════════════════
//  AdminPanel.hpp — Админ-панель NeuroGuard
//  Только для ADMIN_ID = 8362261813
//  Подключить в main.cpp:
//    #include "AdminPanel.hpp"
//    AdminPanel admin(bot, API_URL);
//    admin.registerHandlers();  // после всех bot.getEvents()
// ═══════════════════════════════════════════════════════════

#include <tgbot/tgbot.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <string>
#include <sstream>
#include <iomanip>
#include <functional>

using json = nlohmann::json;

class AdminPanel {
public:
    static constexpr long long ADMIN_ID = "UR_TGID";

    AdminPanel(TgBot::Bot& bot, const std::string& apiUrl)
        : m_bot(bot), m_apiUrl(apiUrl) {}

    // Вызвать после регистрации всех обычных хендлеров
    void registerHandlers() {
        // /admin — главное меню
        m_bot.getEvents().onCommand("admin", [this](TgBot::Message::Ptr m) {
            if (m->chat->id != ADMIN_ID) return;
            sendAdminMenu(ADMIN_ID);
        });

        // /broadcast — начать рассылку
        m_bot.getEvents().onCommand("broadcast", [this](TgBot::Message::Ptr m) {
            if (m->chat->id != ADMIN_ID) return;
            m_awaitingBroadcast = true;
            m_bot.getApi().sendMessage(ADMIN_ID,
                "📢 <b>Рассылка</b>\n\nВведите текст сообщения.\n"
                "Поддерживается HTML разметка.\n\n"
                "Отправьте <code>отмена</code> для отмены.",
                nullptr, nullptr, nullptr, "HTML");
        });

        // Обработка callback от админ-панели
        m_bot.getEvents().onCallbackQuery([this](TgBot::CallbackQuery::Ptr q) {
            if (q->message->chat->id != ADMIN_ID) return;
            handleAdminCallback(q);
        });

        // Обработка текстовых сообщений от админа
        m_bot.getEvents().onAnyMessage([this](TgBot::Message::Ptr m) {
            if (m->chat->id != ADMIN_ID) return;
            if (m->text.empty() || m->text[0] == '/') return;
            handleAdminText(m);
        });
    }

private:
    TgBot::Bot&  m_bot;
    std::string  m_apiUrl;
    bool         m_awaitingBroadcast    = false;
    bool         m_awaitingGiveRank     = false;  // шаг 1: ждём tg_id
    bool         m_awaitingGiveRankStep2= false;  // шаг 2: ждём rank+days
    std::string  m_giveRankTgId         = "";
    std::string  m_broadcastPreview     = "";

    // ── Главное меню ─────────────────────────────────────────
    void sendAdminMenu(long long cid) {
        // Получаем статистику
        auto r = cpr::Get(cpr::Url{m_apiUrl + "/admin/stats"},
                          cpr::Header{
                              {"ngrok-skip-browser-warning","true"},
                              {"api-token", std::getenv("NEUROGUARD_INTERNAL_API_TOKEN")}
                          },
                          cpr::Timeout{5000});

        std::stringstream ss;
        ss << "🎛 <b>ADMIN PANEL — NeuroGuard</b>\n";
        ss << "<code>──────────────────────────</code>\n\n";

        if (r.status_code == 200) {
            try {
                auto j = json::parse(r.text);
                ss << "📊 <b>Статистика:</b>\n";
                ss << "├ Всего пользователей: <code>" << j.value("total_users", 0) << "</code>\n";
                ss << "├ FREE: <code>"      << j.value("free_count",     0) << "</code>\n";
                ss << "├ SENTINEL: <code>"  << j.value("sentinel_count", 0) << "</code>\n";
                ss << "├ OVERSEER: <code>"  << j.value("overseer_count", 0) << "</code>\n";
                ss << "├ Онлайн агентов: <code>" << j.value("online_agents", 0) << "</code>\n";
                ss << "└ «Висяков» (нет tg_id): <code>" << j.value("hanging_agents", 0) << "</code>\n\n";
            } catch (...) {
                ss << "⚠️ Статистика недоступна\n\n";
            }
        } else {
            // Если эндпоинта ещё нет — показываем меню без статистики
            ss << "⚠️ <i>Эндпоинт /admin/stats не добавлен в бэкенд</i>\n\n";
        }

        ss << "👇 Выберите действие:";

        TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);

        auto makeBtn = [](const std::string& text, const std::string& data) {
            auto b = std::make_shared<TgBot::InlineKeyboardButton>();
            b->text = text; b->callbackData = data;
            return b;
        };

        kb->inlineKeyboard.push_back({makeBtn("👥 Все пользователи", "adm_users_0")});
        kb->inlineKeyboard.push_back({makeBtn("🖥 Все агенты",       "adm_agents_0")});
        kb->inlineKeyboard.push_back({makeBtn("🎖 Выдать подписку",  "adm_give_rank")});
        kb->inlineKeyboard.push_back({makeBtn("❌ Снять подписку",   "adm_remove_rank")});
        kb->inlineKeyboard.push_back({makeBtn("📢 Рассылка",         "adm_broadcast")});
        kb->inlineKeyboard.push_back({makeBtn("🔄 Обновить",         "adm_refresh")});

        m_bot.getApi().sendMessage(cid, ss.str(), nullptr, nullptr, kb, "HTML");
    }

    // ── Обработка callback ───────────────────────────────────
    void handleAdminCallback(TgBot::CallbackQuery::Ptr q) {
        try { m_bot.getApi().answerCallbackQuery(q->id); } catch (...) {}

        const std::string& d = q->data;
        if (d.empty()) return;

        if (d == "adm_refresh") {
            sendAdminMenu(ADMIN_ID);
            return;
        }

        if (d == "adm_broadcast") {
            m_awaitingBroadcast = true;
            m_bot.getApi().sendMessage(ADMIN_ID,
                "📢 <b>Рассылка</b>\n\nВведите текст сообщения (HTML):\n\n"
                "<code>отмена</code> — отменить",
                nullptr, nullptr, nullptr, "HTML");
            return;
        }

        if (d == "adm_give_rank") {
            m_awaitingGiveRank = true;
            m_bot.getApi().sendMessage(ADMIN_ID,
                "🎖 <b>Выдать подписку</b>\n\nВведите tg_id пользователя:",
                nullptr, nullptr, nullptr, "HTML");
            return;
        }

        if (d == "adm_remove_rank") {
            m_awaitingGiveRank = true;
            m_giveRankTgId = "__remove__";
            m_bot.getApi().sendMessage(ADMIN_ID,
                "❌ <b>Снять подписку</b>\n\nВведите tg_id пользователя:",
                nullptr, nullptr, nullptr, "HTML");
            return;
        }

        // Пагинация пользователей: adm_users_PAGE
        if (d.rfind("adm_users_", 0) == 0) {
            int page = 0;
            try { page = std::stoi(d.substr(10)); } catch (...) {}
            showUsers(page);
            return;
        }

        // Пагинация агентов: adm_agents_PAGE
        if (d.rfind("adm_agents_", 0) == 0) {
            int page = 0;
            try { page = std::stoi(d.substr(11)); } catch (...) {}
            showAgents(page);
            return;
        }

        // Подтверждение рассылки
        if (d == "adm_broadcast_confirm") {
            doBroadcast(m_broadcastPreview);
            m_broadcastPreview = "";
            return;
        }

        if (d == "adm_broadcast_cancel") {
            m_broadcastPreview = "";
            m_bot.getApi().sendMessage(ADMIN_ID, "❌ Рассылка отменена.");
            return;
        }

        // Выдача ранга: adm_set_TGID_RANK_DAYS
        if (d.rfind("adm_set_", 0) == 0) {
            // формат: adm_set_TGID_RANK_DAYS
            std::string rest = d.substr(8);
            auto p1 = rest.find('_');
            auto p2 = rest.rfind('_');
            if (p1 != std::string::npos && p2 != p1) {
                std::string tgid = rest.substr(0, p1);
                std::string rank = rest.substr(p1+1, p2-p1-1);
                std::string days = rest.substr(p2+1);
                giveRank(tgid, rank, days);
            }
            return;
        }
    }

    // ── Обработка текста от админа ───────────────────────────
    void handleAdminText(TgBot::Message::Ptr m) {
        std::string text = m->text;

        // Рассылка — шаг 1: получили текст
        if (m_awaitingBroadcast) {
            m_awaitingBroadcast = false;
            if (text == "отмена" || text == "Отмена") {
                m_bot.getApi().sendMessage(ADMIN_ID, "❌ Рассылка отменена.");
                return;
            }
            m_broadcastPreview = text;

            // Показываем превью с подтверждением
            TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);
            auto bOk = std::make_shared<TgBot::InlineKeyboardButton>();
            bOk->text = "✅ Отправить всем"; bOk->callbackData = "adm_broadcast_confirm";
            auto bNo = std::make_shared<TgBot::InlineKeyboardButton>();
            bNo->text = "❌ Отмена"; bNo->callbackData = "adm_broadcast_cancel";
            kb->inlineKeyboard.push_back({bOk, bNo});

            m_bot.getApi().sendMessage(ADMIN_ID,
                "📢 <b>Превью рассылки:</b>\n\n" + text +
                "\n\n──────────────────\n<i>Подтвердите отправку:</i>",
                nullptr, nullptr, kb, "HTML");
            return;
        }

        // Выдача ранга — шаг 1: получили tg_id
        if (m_awaitingGiveRank && m_giveRankTgId.empty()) {
            m_awaitingGiveRank = false;

            if (text == "отмена") {
                m_bot.getApi().sendMessage(ADMIN_ID, "❌ Отменено.");
                return;
            }

            m_giveRankTgId = text;

            // Если режим снятия
            if (m_giveRankTgId == "__remove__") {
                m_giveRankTgId = text;
                giveRank(text, "FREE", "0");
                return;
            }

            m_awaitingGiveRankStep2 = true;

            TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);
            auto makeBtn = [&](const std::string& t, const std::string& rank, const std::string& days) {
                auto b = std::make_shared<TgBot::InlineKeyboardButton>();
                b->text = t;
                b->callbackData = "adm_set_" + m_giveRankTgId + "_" + rank + "_" + days;
                return b;
            };

            kb->inlineKeyboard.push_back({
                makeBtn("🛡 SENTINEL 30д", "SENTINEL", "30"),
                makeBtn("👑 OVERSEER 30д", "OVERSEER", "30")
            });
            kb->inlineKeyboard.push_back({
                makeBtn("🛡 SENTINEL 7д",  "SENTINEL", "7"),
                makeBtn("👑 OVERSEER 7д",  "OVERSEER", "7")
            });
            kb->inlineKeyboard.push_back({
                makeBtn("👑 OVERSEER 365д","OVERSEER", "365"),
                makeBtn("❌ FREE (снять)", "FREE",     "0")
            });

            m_bot.getApi().sendMessage(ADMIN_ID,
                "🎖 <b>Выберите ранг для</b> <code>" + m_giveRankTgId + "</code>:",
                nullptr, nullptr, kb, "HTML");
            return;
        }
    }

    // ── Показ пользователей (пагинация по 10) ────────────────
    void showUsers(int page) {
        auto r = cpr::Get(cpr::Url{m_apiUrl + "/admin/users?page=" + std::to_string(page)},
                          cpr::Header{
                              {"ngrok-skip-browser-warning", "true"},
                              {"api-token", std::getenv("NEUROGUARD_INTERNAL_API_TOKEN")}
                          },
                          cpr::Timeout{8000});

        if (r.status_code != 200) {
            m_bot.getApi().sendMessage(ADMIN_ID,
                "⚠️ Эндпоинт <code>/admin/users</code> не добавлен в бэкенд.\n\n"
                "Добавь его согласно инструкции в README.",
                nullptr, nullptr, nullptr, "HTML");
            return;
        }

        try {
            auto j = json::parse(r.text);
            auto users = j["users"];
            int total  = j.value("total", 0);
            int pages  = (total + 9) / 10;

            std::stringstream ss;
            ss << "👥 <b>Пользователи</b> (стр. " << page+1 << "/" << pages << ")\n";
            ss << "<code>──────────────────────────</code>\n";

            for (auto &u : users) {
                std::string rank    = u.value("rank", "FREE");
                std::string tgid    = u.value("tg_id", "?");
                std::string sub_end = u.value("subscription_end", "—");
                std::string reg     = u.value("created_at", "?");

                std::string emoji = "🆓";
                if (rank == "SENTINEL") emoji = "🛡";
                if (rank == "OVERSEER") emoji = "👑";

                ss << emoji << " <code>" << tgid << "</code> | " << rank;
                if (sub_end != "—" && !sub_end.empty())
                    ss << " до <code>" << sub_end << "</code>";
                ss << "\n";
            }

            TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);
            std::vector<TgBot::InlineKeyboardButton::Ptr> navRow;

            if (page > 0) {
                auto bPrev = std::make_shared<TgBot::InlineKeyboardButton>();
                bPrev->text = "◀️"; bPrev->callbackData = "adm_users_" + std::to_string(page-1);
                navRow.push_back(bPrev);
            }
            if (page < pages-1) {
                auto bNext = std::make_shared<TgBot::InlineKeyboardButton>();
                bNext->text = "▶️"; bNext->callbackData = "adm_users_" + std::to_string(page+1);
                navRow.push_back(bNext);
            }
            if (!navRow.empty()) kb->inlineKeyboard.push_back(navRow);

            auto bBack = std::make_shared<TgBot::InlineKeyboardButton>();
            bBack->text = "◀️ Меню"; bBack->callbackData = "adm_refresh";
            kb->inlineKeyboard.push_back({bBack});

            m_bot.getApi().sendMessage(ADMIN_ID, ss.str(), nullptr, nullptr, kb, "HTML");
        } catch (...) {
            m_bot.getApi().sendMessage(ADMIN_ID, "❌ Ошибка парсинга ответа.");
        }
    }

    // ── Показ агентов ─────────────────────────────────────────
    void showAgents(int page) {
        auto r = cpr::Get(cpr::Url{m_apiUrl + "/admin/agents?page=" + std::to_string(page)},
                          cpr::Header{
                              {"ngrok-skip-browser-warning", "true"},
                              {"api-token", std::getenv("NEUROGUARD_INTERNAL_API_TOKEN")}
                          },
                          cpr::Timeout{8000});

        if (r.status_code != 200) {
            m_bot.getApi().sendMessage(ADMIN_ID,
                "⚠️ Эндпоинт <code>/admin/agents</code> не добавлен в бэкенд.",
                nullptr, nullptr, nullptr, "HTML");
            return;
        }

        try {
            auto j      = json::parse(r.text);
            auto agents = j["agents"];
            int total   = j.value("total", 0);
            int pages   = (total + 9) / 10;

            std::stringstream ss;
            ss << "🖥 <b>Агенты</b> (стр. " << page+1 << "/" << pages << ")\n";
            ss << "<code>──────────────────────────</code>\n";

            for (auto &a : agents) {
                std::string hwid     = a.value("hwid", "?");
                std::string name     = a.value("name", "?");
                std::string tgid     = a.value("tg_id", "—");
                std::string rank     = a.value("rank",  "FREE");
                std::string lastSeen = a.value("last_seen", "?");
                bool        online   = a.value("online", false);

                ss << (online ? "🟢 " : "🔴 ");
                ss << "<b>" << name << "</b>\n";
                ss << "├ HWID: <code>" << hwid.substr(0,12) << "...</code>\n";
                ss << "├ TG: <code>" << tgid << "</code> | " << rank << "\n";
                ss << "└ Last: <code>" << lastSeen << "</code>\n\n";
            }

            TgBot::InlineKeyboardMarkup::Ptr kb(new TgBot::InlineKeyboardMarkup);
            std::vector<TgBot::InlineKeyboardButton::Ptr> navRow;

            if (page > 0) {
                auto bPrev = std::make_shared<TgBot::InlineKeyboardButton>();
                bPrev->text = "◀️"; bPrev->callbackData = "adm_agents_" + std::to_string(page-1);
                navRow.push_back(bPrev);
            }
            if (page < pages-1) {
                auto bNext = std::make_shared<TgBot::InlineKeyboardButton>();
                bNext->text = "▶️"; bNext->callbackData = "adm_agents_" + std::to_string(page+1);
                navRow.push_back(bNext);
            }
            if (!navRow.empty()) kb->inlineKeyboard.push_back(navRow);

            auto bBack = std::make_shared<TgBot::InlineKeyboardButton>();
            bBack->text = "◀️ Меню"; bBack->callbackData = "adm_refresh";
            kb->inlineKeyboard.push_back({bBack});

            m_bot.getApi().sendMessage(ADMIN_ID, ss.str(), nullptr, nullptr, kb, "HTML");
        } catch (...) {
            m_bot.getApi().sendMessage(ADMIN_ID, "❌ Ошибка парсинга ответа.");
        }
    }

    // ── Выдача ранга ─────────────────────────────────────────
    void giveRank(const std::string& tgid, const std::string& rank, const std::string& days) {
        m_giveRankTgId = "";
        m_awaitingGiveRankStep2 = false;

        const char* api_token = std::getenv("NEUROGUARD_INTERNAL_API_TOKEN");
        if (!api_token) {
            m_bot.getApi().sendMessage(ADMIN_ID, "❌ NEUROGUARD_INTERNAL_API_TOKEN не задан.");
            return;
        }

        auto r = cpr::Post(cpr::Url{m_apiUrl + "/bot/upgrade"},
                           cpr::Payload{
                               {"tg_id",     tgid},
                               {"rank",      rank},
                               {"days",      days},
                               {"api_token", std::string(api_token)}
                           },
                           cpr::Header{{"ngrok-skip-browser-warning","true"}},
                           cpr::Timeout{5000});

        if (r.status_code == 200) {
            std::string emoji = (rank == "OVERSEER") ? "👑" : (rank == "SENTINEL") ? "🛡" : "🆓";
            m_bot.getApi().sendMessage(ADMIN_ID,
                "✅ <b>Готово!</b>\n\n"
                "Пользователь: <code>" + tgid + "</code>\n"
                "Ранг: " + emoji + " <b>" + rank + "</b>\n"
                "Дней: <code>" + days + "</code>",
                nullptr, nullptr, nullptr, "HTML");

            // Уведомляем пользователя
            try {
                long long uid = std::stoll(tgid);
                std::string msg;
                if (rank == "FREE") {
                    msg = "ℹ️ Ваша подписка была изменена администратором.";
                } else {
                    std::string e2 = (rank == "OVERSEER") ? "👑" : "🛡";
                    msg = e2 + " <b>Подписка " + rank + " активирована!</b>\n\n"
                          "Действует <b>" + days + " дней</b>.\n"
                          "Удачи! 🚀";
                }
                m_bot.getApi().sendMessage(uid, msg, nullptr, nullptr, nullptr, "HTML");
            } catch (...) {}
        } else {
            m_bot.getApi().sendMessage(ADMIN_ID,
                "❌ Ошибка: " + std::to_string(r.status_code) + "\n" + r.text.substr(0, 200));
        }
    }

    // ── Рассылка всем пользователям ──────────────────────────
    void doBroadcast(const std::string& text) {
        if (text.empty()) return;

        // Получаем всех пользователей
        auto r = cpr::Get(cpr::Url{m_apiUrl + "/admin/all_user_ids"},
                          cpr::Header{
                              {"ngrok-skip-browser-warning", "true"},
                              {"api-token", std::getenv("NEUROGUARD_INTERNAL_API_TOKEN")}
                          },
                          cpr::Timeout{10000});

        if (r.status_code != 200) {
            // Если эндпоинта нет — сообщаем
            m_bot.getApi().sendMessage(ADMIN_ID,
                "⚠️ Эндпоинт <code>/admin/all_user_ids</code> не добавлен в бэкенд.\n\n"
                "Добавь его в main.py:\n"
                "<code>@app.get(\"/admin/all_user_ids\")\n"
                "async def all_user_ids(db=Depends(get_db)):\n"
                "    res = await db.execute(select(User.tg_id))\n"
                "    return {\"ids\": [r[0] for r in res.all()]}</code>",
                nullptr, nullptr, nullptr, "HTML");
            return;
        }

        try {
            auto j = json::parse(r.text);
            auto ids = j["ids"];

            int sent = 0, failed = 0;
            m_bot.getApi().sendMessage(ADMIN_ID,
                "📢 Начинаю рассылку для <code>" + std::to_string(ids.size()) + "</code> пользователей...");

            for (auto& idVal : ids) {
                try {
                    long long uid = 0;
                    if (idVal.is_string())
                        uid = std::stoll(idVal.get<std::string>());
                    else
                        uid = idVal.get<long long>();

                    if (uid <= 0) continue;

                    m_bot.getApi().sendMessage(uid, text, nullptr, nullptr, nullptr, "HTML");
                    sent++;

                    // Задержка чтобы не словить flood от Telegram (30 msg/sec макс)
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                } catch (...) { failed++; }
            }

            m_bot.getApi().sendMessage(ADMIN_ID,
                "✅ <b>Рассылка завершена</b>\n\n"
                "├ Отправлено: <code>" + std::to_string(sent)   + "</code>\n"
                "└ Ошибок:    <code>" + std::to_string(failed) + "</code>",
                nullptr, nullptr, nullptr, "HTML");
        } catch (...) {
            m_bot.getApi().sendMessage(ADMIN_ID, "❌ Ошибка при рассылке.");
        }
    }
};