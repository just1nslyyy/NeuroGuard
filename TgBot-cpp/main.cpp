#include <memory>
#include <tgbot/tgbot.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <thread>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <cstdlib>
#include <stdexcept>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "SubscriptionModel.hpp"
#include "SubFactory.hpp"
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <csignal>
#include <limits>
#include "AdminPanel.hpp"

using namespace std;
using namespace TgBot;

std::string base64_encode(const std::string &in) {
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c: in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::mutex globalMutex;
std::map<long long, string> awaitingPaymentId;
std::map<long long, string> awaitingPaymentRank;
std::map<long long, bool> awaitingName;
std::map<long long, string> renamingDeviceId;
std::map<long long, std::shared_ptr<ISubscription> > userSubs;
std::map<long long, std::chrono::steady_clock::time_point> lastActionTime;
std::map<long long, string> userPins;
std::map<long long, std::pair<string, string> > awaitingPin;
std::set<long long> awaitingSetPin;
std::set<long long> awaitingRemovePinConfirm;
std::map<long long, std::pair<string, string> > autoSleepSchedule;
std::map<long long, string> autoSleepFiredDate;
std::map<long long, string> awaitingAutoSleepTime;

std::atomic<bool> running(true);

const string API_URL = "U_API";
const string PHOTO_MAIN = "Images/MainMenu-2.png";
const string PHOTO_SUB = "Images/Subscription.png";
const string PHOTO_SUPP = "Images/Support.png";
const string PHOTO_FAQ = "Images/FAQ.png";
const string PHOTO_PROFILE = "Images/PROFILE.png";
const string EULA_URL =
        "https://docs.google.com/document/d/1l-FpslFh7d-ICGmJX8UegVe6m-9UuAlMvGWUEGL_ai8/edit?usp=sharing";
const string NEWS_CHANNEL = "https://t.me/NeuroGuardNews";

void logError(const string &ctx, const string &err) {
    cerr << "[ERROR] " << ctx << ": " << err << endl;
}

void logInfo(const string &ctx, const string &msg) {
    cout << "[INFO] " << ctx << ": " << msg << endl;
}

bool fileExists(const string &path) {
    return std::filesystem::exists(path);
}

bool checkRateLimit(long long tgId, int ms = 1500) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(globalMutex);
    auto it = lastActionTime.find(tgId);
    if (it != lastActionTime.end() &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count() < ms)
        return false;
    lastActionTime[tgId] = now;
    return true;
}

std::string to_fixed(float v, int p) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(p) << v;
    return o.str();
}

KeyboardButton::Ptr makeButton(const string &t) {
    auto b = make_shared<KeyboardButton>();
    b->text = t;
    return b;
}

void sendPhoto(TgBot::Bot &bot, long long cid, const string &path,
               const string &cap, TgBot::GenericReply::Ptr kb = nullptr) {
    try {
        if (!fileExists(path)) {
            logError("sendPhoto", "File not found: " + path);
            bot.getApi().sendMessage(cid, cap, nullptr, nullptr, kb, "HTML");
            return;
        }
        bot.getApi().sendPhoto(cid, TgBot::InputFile::fromFile(path, "image/png"), cap, 0, kb, "HTML");
    } catch (const exception &e) {
        logError("sendPhoto", e.what());
        try {
            bot.getApi().sendMessage(cid, cap, nullptr, nullptr, kb, "HTML");
        } catch (const exception &e2) {
            logError("sendPhoto fallback", e2.what());
        }
    }
}

std::pair<int, int> parseTime(const string &s) {
    if (s.size() != 5 || s[2] != ':') return {-1, -1};
    try {
        int h = stoi(s.substr(0, 2)), m = stoi(s.substr(3, 2));
        if (h < 0 || h > 23 || m < 0 || m > 59) return {-1, -1};
        return {h, m};
    } catch (...) { return {-1, -1}; }
}

bool safeStoi(const string &str, int &result) {
    try {
        long long val = std::stoll(str);
        if (val < std::numeric_limits<int>::min() || val > std::numeric_limits<int>::max()) {
            return false;
        }
        result = (int) val;
        return true;
    } catch (...) {
        return false;
    }
}

void autoSleepThread(TgBot::Bot &bot, std::atomic<bool> &running) {
    logInfo("autoSleepThread", "Started");
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        try {
            auto now = std::time(nullptr);
            auto *tu = std::gmtime(&now);
            int ch = (tu->tm_hour + 3) % 24, cm = tu->tm_min;
            auto msk = now + 3 * 3600;
            auto *td = std::gmtime(&msk);
            char db[16];
            std::strftime(db, sizeof(db), "%Y-%m-%d", td);
            string today(db);

            map<long long, pair<string, string> > scheduleSnapshot;
            map<long long, string> firedSnapshot;
            {
                std::lock_guard<std::mutex> lock(globalMutex);
                scheduleSnapshot = autoSleepSchedule;
                firedSnapshot = autoSleepFiredDate;
            }

            for (auto &[tid, sch]: scheduleSnapshot) {
                if (!running) break;
                try {
                    auto [hwid, ts] = sch;
                    auto [sh, sm] = parseTime(ts);
                    if (sh < 0 || ch != sh || cm != sm) continue;
                    if (firedSnapshot.count(tid) && firedSnapshot[tid] == today) continue;

                    cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                              cpr::Parameters{{"hwid", hwid}, {"command", "sleep"}, {"tg_id", to_string(tid)}},
                              cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                              cpr::Timeout{5000});

                    InlineKeyboardMarkup::Ptr kb(new InlineKeyboardMarkup);
                    auto btn = make_shared<InlineKeyboardButton>();
                    btn->text = "❌ Отменить сон";
                    btn->callbackData = "cancel_sleep_" + hwid;
                    kb->inlineKeyboard.push_back({btn});

                    bot.getApi().sendMessage(tid,
                                             "🌙 <b>Авто-сон:</b> ПК <code>" + hwid.substr(0, 8) + "...</code>\n"
                                             "Время: <b>" + ts + " МСК</b>", nullptr, nullptr, kb, "HTML");

                    {
                        std::lock_guard<std::mutex> lock(globalMutex);
                        autoSleepFiredDate[tid] = today;
                    }
                } catch (const exception &e) {
                    logError("autoSleepThread sendMessage", e.what());
                }
            }
        } catch (const exception &e) { logError("autoSleepThread", e.what()); }
    }
}

void subscriptionAlertThread(TgBot::Bot &bot, std::atomic<bool> &running) {
    logInfo("subscriptionAlertThread", "Started");
    while (running) {
        std::this_thread::sleep_for(std::chrono::hours(12));
        if (!running) break;

        try {
            auto r = cpr::Get(cpr::Url{API_URL + "/bot/expiring_subscriptions"},
                              cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                              cpr::Timeout{10000});
            if (r.status_code != 200) continue;

            auto alerts = nlohmann::json::parse(r.text);
            for (auto &e: alerts) {
                if (!running) break;
                try {
                    long long tid = e.value("tg_id", 0LL);
                    if (!tid) continue;

                    bot.getApi().sendMessage(tid,
                                             "⏰ <b>Подписка заканчивается!</b>\n\n"
                                             "Истекает <b>" + e.value("end_date", string("?")) + "</b>"
                                             " (через <b>" + to_string(e.value("days_left", 0)) + " дн.</b>)\n\n"
                                             "👉 /subscribe", nullptr, nullptr, nullptr, "HTML");
                } catch (const exception &e) {
                    logError("subscriptionAlertThread sendMessage", e.what());
                }
            }
        } catch (const exception &e) { logError("subscriptionAlertThread", e.what()); }
    }
}


InlineKeyboardMarkup::Ptr buildDeviceKeyboard(
    const string &hwid, bool online,
    const std::shared_ptr<ISubscription> &sub, bool hasAS = false) {
    InlineKeyboardMarkup::Ptr kb(new InlineKeyboardMarkup);

    auto bRef = make_shared<InlineKeyboardButton>();
    bRef->text = "🔄 Обновить";
    bRef->callbackData = "refresh_stats:" + hwid;
    auto bSet = make_shared<InlineKeyboardButton>();
    bSet->text = "⚙️ Опции";
    bSet->callbackData = "setup_" + hwid;
    kb->inlineKeyboard.push_back({bRef, bSet});

    if (online && sub->canControlPower()) {
        auto bOff = make_shared<InlineKeyboardButton>();
        bOff->text = "🔌 Выключить";
        bOff->callbackData = "off_" + hwid;
        auto bRev = make_shared<InlineKeyboardButton>();
        bRev->text = "↩ Отменить выкл";
        bRev->callbackData = "revokecmd_" + hwid;
        kb->inlineKeyboard.push_back({bOff, bRev});
    }

    if (online && sub->canTakeScreenshots()) {
        auto bSlp = make_shared<InlineKeyboardButton>();
        bSlp->text = "🌙 Сон";
        bSlp->callbackData = "sleep_" + hwid;
        auto bPrc = make_shared<InlineKeyboardButton>();
        bPrc->text = "📊 Процессы";
        bPrc->callbackData = "get_procs_" + hwid;
        kb->inlineKeyboard.push_back({bSlp, bPrc});

        auto bScr = make_shared<InlineKeyboardButton>();
        bScr->text = "📸 Скриншот";
        bScr->callbackData = "scr_" + hwid;
        kb->inlineKeyboard.push_back({bScr});

        auto bAS = make_shared<InlineKeyboardButton>();
        if (hasAS) {
            bAS->text = "⏰ Авто-сон: ВКЛ";
            bAS->callbackData = "autosleep_off_" + hwid;
        } else {
            bAS->text = "⏰ Авто-сон";
            bAS->callbackData = "autosleep_set_" + hwid;
        }
        kb->inlineKeyboard.push_back({bAS});
    }

    auto bH = make_shared<InlineKeyboardButton>();
    bH->text = "🏥 Состояние ПК";
    bH->callbackData = "health_" + hwid;
    kb->inlineKeyboard.push_back({bH});

    auto bBk = make_shared<InlineKeyboardButton>();
    bBk->text = "◀️ Назад к списку";
    bBk->callbackData = "back_to_status";
    kb->inlineKeyboard.push_back({bBk});

    return kb;
}


void showMesProfile(TgBot::Bot &bot, TgBot::Message::Ptr message) {
    long long cid = message->chat->id;
    auto r = cpr::Get(cpr::Url{API_URL + "/bot/status?tg_id=" + to_string(cid)},
                      cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                      cpr::Timeout{5000});
    if (r.status_code != 200) {
        bot.getApi().sendMessage(cid, "❌ Сервер недоступен.");
        return;
    }
    try {
        auto j = nlohmann::json::parse(r.text);
        string rank = j.value("user_rank", "FREE");

        std::shared_ptr<ISubscription> sub;
        {
            std::lock_guard<std::mutex> lock(globalMutex);
            sub = SubFactory::create(rank);
            if (!sub) {
                logError("showMesProfile", "SubFactory returned nullptr");
                bot.getApi().sendMessage(cid, "❌ Ошибка инициализации.");
                return;
            }
            userSubs[cid] = sub;
        }

        auto devs = j.contains("devices") ? j["devices"] : nlohmann::json::array();

        if (!message->from) {
            auto from = make_shared<TgBot::User>();
            from->id = cid;
            from->firstName = "User";
            from->isBot = false;
            sub->displayProfile(bot, from, devs, PHOTO_PROFILE);
        } else {
            sub->displayProfile(bot, message->from, devs, PHOTO_PROFILE);
        }
    } catch (const exception &e) { logError("showMesProfile", e.what()); }
}


void showMesStatus(TgBot::Bot &bot, long long tgId) {
    auto r = cpr::Get(cpr::Url{API_URL + "/bot/status?tg_id=" + to_string(tgId)},
                      cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                      cpr::Timeout{5000});
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
                b->text = "⏭ Пропустить (250W)";
                b->callbackData = "psu_skip";
                kb->inlineKeyboard.push_back({b});
                bot.getApi().sendMessage(tgId,
                                         "⚡ <b>Укажите мощность БП</b>\nПример: <code>/psu 550</code>",
                                         nullptr, nullptr, kb, "HTML");
                return;
            }
        }

        std::shared_ptr<ISubscription> sub;
        {
            std::lock_guard<std::mutex> lock(globalMutex);
            sub = SubFactory::create(rank);
            if (!sub) {
                logError("showMesStatus", "SubFactory returned nullptr for rank: " + rank);
                bot.getApi().sendMessage(tgId, "❌ Ошибка инициализации подписки.");
                return;
            }
            userSubs[tgId] = sub;
        }

        nlohmann::json devs = nlohmann::json::array();
        if (j.contains("devices") && j["devices"].is_array()) devs = j["devices"];

        if (devs.empty()) {
            bot.getApi().sendMessage(tgId,
                                     "📡 <b>Устройства не обнаружены.</b>\n\n"
                                     "Нажми /download чтобы установить агента.",
                                     nullptr, nullptr, nullptr, "HTML");
            return;
        }

        if (devs.size() == 1) {
            auto dev = devs[0];
            string hwid = dev.value("hwid", "");
            bool online = dev.value("online", false);
            bool hasAS = false;
            {
                std::lock_guard<std::mutex> lock(globalMutex);
                hasAS = autoSleepSchedule.count(tgId) && autoSleepSchedule[tgId].first == hwid;
            }
            auto kb = buildDeviceKeyboard(hwid, online, sub, hasAS);
            bot.getApi().sendMessage(tgId, sub->formatStatus(dev), nullptr, nullptr, kb, "HTML");
            return;
        }

        InlineKeyboardMarkup::Ptr kb(new InlineKeyboardMarkup);
        string lt = "🖥 <b>NETWORK DASHBOARD</b>\n──────────────────────────\n";
        for (auto &dev: devs) {
            bool on = dev.value("online", false);
            string nm = dev.value("name", "PC"), hw = dev.value("hwid", "");
            lt += (on ? "🟢 " : "🔴 ") + nm + " [<code>" + hw.substr(0, 8) + "...</code>]\n";
            auto b = make_shared<InlineKeyboardButton>();
            b->text = (on ? "⚡️ " : "📁 ") + nm;
            b->callbackData = "refresh_stats:" + hw;
            kb->inlineKeyboard.push_back({b});
        }
        lt += "──────────────────────────\n💎 <b>" + rank + "</b>";
        bot.getApi().sendMessage(tgId, lt, nullptr, nullptr, kb, "HTML");
    } catch (const exception &e) {
        logError("showMesStatus", e.what());
        bot.getApi().sendMessage(tgId, "❌ Ошибка обработки данных.");
    }
}

int main() {
    const char *shop_id = std::getenv("YOOKASSA_SHOP_ID");
    const char *secret_key = std::getenv("YOOKASSA_SECRET_KEY");
    if (!shop_id || !secret_key) {
        cerr << "YOOKASSA_SHOP_ID or YOOKASSA_SECRET_KEY not set" << endl;
        return 1;
    }


    const char *te = std::getenv("NEUROGUARD_BOT_TOKEN");
    if (!te || string(te).size() < 30) {
        cerr << "NEUROGUARD_BOT_TOKEN invalid" << endl;
        return 1;
    }
    Bot bot(te);

    AdminPanel adminPanel(bot, API_URL);


    logInfo("main", "Bot started");

    const string welcomeMessage =
            "<b>👋 Управляй ПК из Telegram!</b>\n"
            "━━━━━━━━━━━━━━━━━━━━━━━━\n\n"
            "🎬 <b>Монтажёр/3D-художник?</b>\n"
            "Рендер идёт 6 часов, ПК перегрелся и выключился?\n"
            "С NeuroGuard такого не будет! ✅\n\n"
            "🎮 <b>Геймер?</b> Выключи ПК удалённо, когда ушёл из дома.\n"
            "💼 <b>Админ?</b> Мониторь 10+ ПК из телефона.\n\n"
            "<b>💎 Что умеет:</b>\n"
            "🚀 Мониторинг CPU/GPU/RAM в реальном времени\n"
            "🌡 Алерты при перегреве (защита оборудования)\n"
            "📸 Скриншоты (OVERSEER)\n"
            "🔌 Выключение / Сон (экономия электричества)\n"
            "🛑 Завершение зависших процессов\n"
            "⏰ Авто-сон по расписанию\n\n"
            "🎁 <b>7 дней OVERSEER БЕСПЛАТНО</b> 🔥\n"
            "<i>Без привязки карты, просто попробуй!</i>\n\n"
            "👇 <b>Начни прямо сейчас:</b>\n"
            "📥 /download — установить агента (2 минуты)\n"
            "📖 /guide — пошаговая инструкция\n\n"
            "━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "⚠️ <i>Используя бота, вы соглашаетесь с "
            "<a href='" + EULA_URL + "'>правилами использования</a></i>\n\n"
            "<i>⚡️ NeuroGuard — твоё железо под защитой 🛡</i>";

    const string downloadText =
        "<b>🚀 NeuroGuard Agent v1.0</b>\n\n"
        "<b>Ваш персональный модуль мониторинга ПК через Telegram</b>\n"
        "<code>━━━━━━━━━━━━━━━━━━━━━━━━</code>\n\n"
        "<b>📦 В архиве:</b>\n"
        "• <code>NeuroGuard_Setup.exe</code> — мастер установки\n\n"
        "<b>⚙️ Системные требования:</b>\n"
        "• Windows 10/11 (x64)\n"
        "• 50 MB свободного места\n\n"
        "<b>⚠️ Важно про антивирусы:</b>\n\n"
        "NeuroGuard использует OpenHardwareMonitor для чтения датчиков CPU/GPU.\n"
        "Некоторые антивирусы (включая Windows Defender) могут ложно сработать\n"
        "на драйвер WinRing0.sys, маркируя его как «HackTool».\n\n"
        "<b>Это НЕ вирус и НЕ RAT.</b>\n"
        "Такие же срабатывания бывают у:\n"
        "• MSI Afterburner\n"
        "• CPU-Z / HWMonitor\n"
        "• AIDA64\n\n"
        "<code>━━━━━━━━━━━━━━━━━━━━━━━━</code>\n\n"
        "<b>🛠 Инструкция по установке:</b>\n\n"
        "1️⃣ Скачайте архив и распакуйте его\n"
        "2️⃣ Запустите <code>NeuroGuard_Setup.exe</code> от имени администратора\n"
        "3️⃣ Следуйте инструкциям мастера установки\n"
        "4️⃣ После установки агент запустится автоматически\n"
        "5️⃣ Скопируйте <b>КОД</b> из окна программы\n"
        "6️⃣ Отправьте боту: <code>/auth ВАШ_КОД</code>\n\n"
        "<b>❓ Проблемы при установке:</b>\n"
        "✉️ Поддержка: @TLEET_BLANT";

    const string support =
            "<b>🛡 NeuroGuard Support</b>\n"
            "━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "👋 <b>Привет!</b> Всегда на связи!\n\n"
            "🚀 <b>Текущая фаза:</b> <code>Public Beta v1.0</code>\n\n"
            "<b>Чем помогаем:</b>\n"
            "🔹 Настройка и исключения антивируса\n"
            "🔹 Разбор данных нагрузки и температур\n"
            "🔹 Вылеты и проблемы с соединением\n\n"
            "<b>🏢 ENTERPRISE:</b>\n"
            "🔹 Тарифы для 50, 100+ ПК\n"
            "🔹 Брендирование агента под ваш клуб\n"
            "🔹 API-интеграция в вашу систему\n\n"
            "✉️ <b>Разработчик:</b> @TLEET_BLANT\n"
            "📢 <b>Новости:</b> <a href='" + NEWS_CHANNEL + "'>@NeuroGuardNews</a>\n\n"
            "<i>Спасибо, что помогаешь делать NeuroGuard лучше! 🛠</i>";

    const string about =
            "<b>🛡 NeuroGuard: Контроль и Безопасность</b>\n"
            "━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "<b>NeuroGuard</b> — экосистема для удалённого администрирования ПК.\n\n"
            "<b>❓ ЧАСТЫЕ ВОПРОСЫ:</b>\n\n"
            "🔹 <b>Безопасность:</b>\n"
            "VPS в Нидерландах, шифрование данных, полная конфиденциальность.\n\n"
            "🔹 <b>Анти-шпионаж:</b>\n"
            "При скриншоте на ПК всегда появляется Alert-уведомление. Любая слежка запрещена.\n\n"
            "🔹 <b>HWID и Лицензия:</b>\n"
            "Подписка привязана к оборудованию (HWID). Обход = пожизненный бан.\n\n"
            "🔹 <b>Функционал:</b>\n"
            "Task Kill, выключение ПК удалённо, авто-сон, мониторинг 24/7.\n\n"
            "🏢 <b>Для бизнеса:</b> ПК-клубы (10+ ПК) — особые условия.\n\n"
            "✉️ <b>Связь:</b> @TLEET_BLANT\n"
            "📢 <a href='" + NEWS_CHANNEL + "'>@NeuroGuardNews</a>\n"
            "<i>v1.0 Public Beta | NeuroGuard Team</i>";

    const string captionText =
            "<b>▋ ГЛАВНОЕ МЕНЮ</b>\n"
            "──────────────────\n"
            "<i>Выберите раздел.</i>";

    const string subCaption =
            "<b>🚀 Тарифы NeuroGuard</b>\n\n"
            "🆓 <b>FREE</b> — 1 ПК, только мониторинг\n"
            "🛡 <b>SENTINEL</b> — 3 ПК, 299₽/мес, управление\n"
            "👑 <b>OVERSEER</b> — 10 ПК, 499₽/мес, скриншоты, процессы\n\n"
            "🎁 <b>Пробный период 7 дней OVERSEER</b> — для новых!";

    const string guideText =
            "╔══════════════════════════════╗\n"
            "║   📖  ГАЙД  NEUROGUARD       ║\n"
            "╚══════════════════════════════╝\n\n"
            "┌─────────────────────────────┐\n"
            "│  🚀  НАЧАЛО РАБОТЫ          │\n"
            "└─────────────────────────────┘\n"
            "1️⃣ /download — скачай архив с установщиком\n"
            "2️⃣ Распакуй архив и запусти <code>NeuroGuard_Setup.exe</code>\n"
            "3️⃣ Следуй инструкциям мастера установки\n"
            "4️⃣ Скопируй код из окна агента\n"
            "5️⃣ Отправь боту: <code>/auth КОД</code>\n"
            "6️⃣ Введи имя ПК — готово! 🎉\n\n"
            "┌─────────────────────────────┐\n"
            "���  🛡  КОМАНДЫ                │\n"
            "└─────────────────────────────┘\n"
            "📊 <code>/status</code> — список устройств\n"
            "💎 <code>/subscribe</code> — тарифы и пробный период\n"
            "🔐 <code>/setpin</code> — пин-код для безопасности\n"
            "🗑 <code>/removepin</code> — удалить пин-код\n"
            "⚡ <code>/psu 550</code> — мощность БП (для расчёта кВт)\n"
            "📖 <code>/guide</code> — этот гайд\n\n"
            "┌─────────────────────────────┐\n"
            "│  🎮  УПРАВЛЕНИЕ ПК          │\n"
            "└─────────────────────────────┘\n"
            "Нажми на устройство в 📊 Статус:\n\n"
            "🔄 <b>Обновить</b> — свежие данные\n"
            "🔌 <b>Выключить</b> — удалённое выключение\n"
            "↩ <b>Отменить выкл</b> — отзыв команды\n"
            "🌙 <b>Сон</b> — режим сна ПК\n"
            "📊 <b>Процессы</b> — топ процессов <i>(OVERSEER)</i>\n"
            "📸 <b>Скриншот</b> — снимок экрана <i>(OVERSEER)</i>\n"
            "⏰ <b>Авто-сон</b> — сон по расписанию <i>(OVERSEER)</i>\n"
            "🏥 <b>Состояние ПК</b> — недельная диагностика\n\n"
            "┌─────────────────────────────┐\n"
            "│  👑  ТАРИФЫ                 │\n"
            "└─────────────────────────────┘\n"
            "🆓 <b>FREE</b>      — 1 ПК, только мониторинг\n"
            "🛡 <b>SENTINEL</b> — 3 ПК, 299₽/мес\n"
            "           управление (вкл/выкл/сон)\n"
            "👑 <b>OVERSEER</b> — 10 ПК, 499₽/мес\n"
            "           скриншоты, процессы, авто-сон\n\n"
            "🎁 <b>7 дней OVERSEER бесплатно</b> — /subscribe\n\n"
            "┌─────────────────────────────┐\n"
            "│  ❓  ЧАСТЫЕ ВОПРОСЫ         │\n"
            "└─────────────────────────────┘\n"
            "<b>Как отвязать ПК?</b>\n"
            "  📊 Статус → ПК → ⚙️ Опции → 🗑 Удалить\n\n"
            "<b>Как выключить авто-сон?</b>\n"
            "  📊 Статус → ПК → ⏰ Авто-сон: ВКЛ (нажать ещё раз)\n\n"
            "<b>ПК показывает ОФФЛАЙН?</b>\n"
            "  Запусти агент от Администратора, проверь интернет\n\n"
            "<b>Скриншот не приходит?</b>\n"
            "  Нужен тариф OVERSEER + запуск агента от Администратора\n\n"
            "<b>Как установить пин-код?</b>\n"
            "  /setpin → введи 4 цифры\n"
            "  Пин запрашивается при выключении, сне и скриншоте\n\n"
            "┌─────────────────────────────┐\n"
            "│  📢  ССЫЛКИ                 │\n"
            "└─────────────────────────────┘\n"
            "📢 <a href='" + NEWS_CHANNEL + "'>Новости и обновления</a>\n"
            "✉️ Поддержка: @TLEET_BLANT\n"
            "⚠️ <a href='" + EULA_URL + "'>Пользовательское соглашение</a>\n\n"
            "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "<i>NeuroGuard — твой ПК под контролем! 🛡</i>";

    ReplyKeyboardMarkup::Ptr mainMenu(new ReplyKeyboardMarkup);
    mainMenu->resizeKeyboard = true;
    mainMenu->keyboard = {
        {makeButton("📊 Статус"), makeButton("ℹ️ FAQ")},
        {makeButton("🆘 Поддержка"), makeButton("💎 Подписка")},
        {makeButton("👤 Профиль")}
    };

    ReplyKeyboardMarkup::Ptr backMenu(new ReplyKeyboardMarkup);
    backMenu->resizeKeyboard = true;
    backMenu->keyboard = {{makeButton("◀️ Вернуться в главное меню")}};

    InlineKeyboardMarkup::Ptr subKeyboard(new InlineKeyboardMarkup);
    {
        auto bt = make_shared<InlineKeyboardButton>();
        bt->text = "🎁 Пробный 7 дней (OVERSEER)";
        bt->callbackData = "trial_overseer";
        auto bs = make_shared<InlineKeyboardButton>();
        bs->text = "🛡 SENTINEL — 299₽/мес";
        bs->callbackData = "buy_sentinel";
        auto bo = make_shared<InlineKeyboardButton>();
        bo->text = "👑 OVERSEER — 499₽/мес";
        bo->callbackData = "buy_overseer";
        subKeyboard->inlineKeyboard = {{bt}, {bs}, {bo}};
    }

    std::thread t1(autoSleepThread, std::ref(bot), std::ref(running));
    std::thread t2(subscriptionAlertThread, std::ref(bot), std::ref(running));
    t1.detach();
    t2.detach();

    bot.getEvents().onCommand("start", [&](Message::Ptr m) {
        long long cid = m->chat->id;

    cpr::Get(cpr::Url{API_URL + "/bot/status?tg_id=" + to_string(cid)},
             cpr::Header{{"ngrok-skip-browser-warning", "true"}},
             cpr::Timeout{5000});
        bot.getApi().sendMessage(m->chat->id, welcomeMessage, nullptr, nullptr, mainMenu, "HTML");
    });

    bot.getEvents().onCommand("download", [&](Message::Ptr m) {
        long long cid = m->chat->id;
        bot.getApi().sendMessage(cid, downloadText, nullptr, nullptr, backMenu, "HTML");

        std::thread([&bot, cid]() {
            try {
                bot.getApi().sendDocument(
                    cid, TgBot::InputFile::fromFile("Archive/NeuroGuard.rar",
                                                    "application/octet-stream"),
                    "", "", nullptr, nullptr, "", false);
            } catch (const exception &e) {
                logError("download", e.what());
                try {
                    bot.getApi().sendMessage(cid, "❌ Файл временно недоступен.");
                } catch (...) {
                }
            }
        }).detach();
    });

    bot.getEvents().onCommand("subscribe", [&](Message::Ptr m) {
        long long cid = m->chat->id;
        if (fileExists(PHOTO_SUB)) {
            bot.getApi().sendPhoto(cid,
                                   TgBot::InputFile::fromFile(PHOTO_SUB, "image/png"),
                                   subCaption, 0, subKeyboard, "HTML");
        } else {
            logError("subscribe", "File not found: " + PHOTO_SUB);
            bot.getApi().sendMessage(cid, subCaption, nullptr, nullptr, subKeyboard, "HTML");
        }
    });

    bot.getEvents().onCommand("guide", [&](Message::Ptr m) {
        InlineKeyboardMarkup::Ptr kb(new InlineKeyboardMarkup);
        auto btnNews = make_shared<InlineKeyboardButton>();
        btnNews->text = "📢 Новости";
        btnNews->url = NEWS_CHANNEL;
        auto btnSupp = make_shared<InlineKeyboardButton>();
        btnSupp->text = "✉️ Поддержка";
        btnSupp->url = "https://t.me/TLEET_BLANT";
        kb->inlineKeyboard.push_back({btnNews, btnSupp});
        bot.getApi().sendMessage(m->chat->id, guideText, nullptr, nullptr, kb, "HTML");
    });

    bot.getEvents().onCommand("setpin", [&](Message::Ptr m) {
        {
            std::lock_guard<std::mutex> lock(globalMutex);
            awaitingSetPin.insert(m->chat->id);
        }
        bot.getApi().sendMessage(m->chat->id,
                                 "🔐 <b>Установка пин-кода</b>\n\n"
                                 "Введите <b>4 цифры</b>.\n"
                                 "Пин будет запрашиваться при выключении, сне и скриншоте ПК.\n\n"
                                 "<i>Удалить пин: /removepin</i>",
                                 nullptr, nullptr, nullptr, "HTML");
    });

    bot.getEvents().onCommand("removepin", [&](Message::Ptr m) {
        long long uid = m->chat->id;
        bool hasPin = false;
        {
            std::lock_guard<std::mutex> lock(globalMutex);
            hasPin = userPins.count(uid) > 0;
        }
        if (!hasPin) {
            bot.getApi().sendMessage(uid,
                                     "ℹ️ У вас не установлен пин-код.",
                                     nullptr, nullptr, nullptr, "HTML");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(globalMutex);
            awaitingRemovePinConfirm.insert(uid);
        }
        bot.getApi().sendMessage(uid,
                                 "🔐 <b>Удаление пин-кода</b>\n\n"
                                 "Введите ваш текущий пин для подтверждения:",
                                 nullptr, nullptr, nullptr, "HTML");
    });

    bot.getEvents().onCommand("psu", [&](Message::Ptr m) {
        long long uid = m->chat->id;
        if (m->text.length() <= 5) {
            bot.getApi().sendMessage(uid,
                                     "⚡ <b>Укажите мощность БП:</b>\n<code>/psu 550</code>",
                                     nullptr, nullptr, nullptr, "HTML");
            return;
        }
        string ws = m->text.substr(5);
        ws.erase(remove(ws.begin(), ws.end(), ' '), ws.end());

        int w = 0;
        if (!safeStoi(ws, w)) {
            bot.getApi().sendMessage(uid, "❌ Пример: <code>/psu 550</code>", nullptr, nullptr, nullptr, "HTML");
            return;
        }

        if (w < 100 || w > 3000) {
            bot.getApi().sendMessage(uid, "❌ Допустимо: 100–3000 Вт.", nullptr, nullptr, nullptr, "HTML");
            return;
        }

        auto r = cpr::Post(cpr::Url{API_URL + "/bot/set_psu"},
                           cpr::Parameters{{"tg_id", to_string(uid)}, {"watts", to_string(w)}},
                           cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                           cpr::Timeout{5000});

        if (r.status_code == 200) {
            bot.getApi().sendMessage(uid,
                                     "✅ Мощность БП: <b>" + to_string(w) + "W</b> сохранена.",
                                     nullptr, nullptr, nullptr, "HTML");
        } else {
            logError("psu", "Status code: " + to_string(r.status_code));
            bot.getApi().sendMessage(uid, "❌ Ошибка сохранения. Попробуйте позже.", nullptr, nullptr, nullptr, "HTML");
        }
    });

    bot.getEvents().onCommand("auth", [&](Message::Ptr m) {
        long long uid = m->chat->id;
        if (m->text.length() <= 6) {
            bot.getApi().sendMessage(uid, "⚠️ Формат: <code>/auth КОД</code>", nullptr, nullptr, nullptr, "HTML");
            return;
        }
        string code = m->text.substr(6);
        code.erase(remove(code.begin(), code.end(), ' '), code.end());

        if (code.length() < 6 || code.length() > 32) {
            bot.getApi().sendMessage(uid, "❌ Код должен быть 6–32 символов.", nullptr, nullptr, nullptr, "HTML");
            return;
        }
        if (!std::all_of(code.begin(), code.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_';
        })) {
            bot.getApi().sendMessage(uid, "❌ Код содержит недопустимые символы.", nullptr, nullptr, nullptr, "HTML");
            return;
        }

        auto r = cpr::Post(cpr::Url{API_URL + "/auth/verify"},
                           cpr::Parameters{{"code", code}, {"tg_id", to_string(uid)}},
                           cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                           cpr::Timeout{5000});

        string det;
        try {
            auto ej = nlohmann::json::parse(r.text);
            if (ej.contains("detail") && ej["detail"].is_string()) det = ej["detail"].get<string>();
        } catch (...) {
            logError("auth", "Failed to parse JSON response");
        }

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
            bot.getApi().sendMessage(uid, "🚫 Лимит устройств. Купите подписку (/subscribe).", nullptr, nullptr,
                                     nullptr, "HTML");
        else if (r.status_code == 404 || det == "invalid_code")
            bot.getApi().sendMessage(uid, "❌ Код не найден. Проверьте агент.", nullptr, nullptr, nullptr, "HTML");
        else
            bot.getApi().sendMessage(uid, "❌ Ошибка (" + to_string(r.status_code) + ")", nullptr, nullptr, nullptr,
                                     "HTML");
    });

    bot.getEvents().onCallbackQuery([&](CallbackQuery::Ptr q) {
        try { bot.getApi().answerCallbackQuery(q->id); } catch (...) {
        }

        long long tgId = q->message->chat->id;
        if (!checkRateLimit(tgId)) {
            try { bot.getApi().answerCallbackQuery(q->id, "⏳ Подождите...", false, "", 1); } catch (...) {
            }
            return;
        }
        const string &d = q->data;
        int mid = q->message->messageId;

        if (d.rfind("refresh_stats:", 0) == 0) {
            if (d.length() < 14) return;
            string hwid = d.substr(14);

            auto r = cpr::Get(cpr::Url{API_URL + "/bot/status?tg_id=" + to_string(tgId)},
                              cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                              cpr::Timeout{5000});
            if (r.status_code != 200) return;
            try {
                auto j = nlohmann::json::parse(r.text);
                string rank = j.value("user_rank", "FREE");
                auto sub = SubFactory::create(rank);

                if (!sub) {
                    logError("refresh_stats", "SubFactory returned nullptr for rank: " + rank);
                    return;
                }

                for (auto &dev: j["devices"]) {
                    if (dev.value("hwid", "") != hwid) continue;
                    bool hasAS = false;
                    {
                        std::lock_guard<std::mutex> lock(globalMutex);
                        hasAS = autoSleepSchedule.count(tgId) && autoSleepSchedule[tgId].first == hwid;
                    }
                    auto kb = buildDeviceKeyboard(hwid, dev.value("online", false), sub, hasAS);
                    try {
                        bot.getApi().editMessageText(sub->formatStatus(dev), tgId, mid, "", "HTML", nullptr, kb);
                    } catch (const exception &e) {
                        logError("refresh_stats edit", e.what());
                    }
                    break;
                }
            } catch (const exception &e) { logError("refresh_stats", e.what()); }
        } else if (d == "back_to_status") {
            showMesStatus(bot, tgId);
        } else if (d == "back_to_profile") {
            auto fm = make_shared<TgBot::Message>();
            fm->chat = q->message->chat;
            fm->from = make_shared<TgBot::User>();
            fm->from->id = q->message->chat->id;
            fm->from->firstName = "User";
            fm->from->isBot = false;
            showMesProfile(bot, fm);
        } else if (d.rfind("setup_", 0) == 0) {
            if (d.length() < 6) return;
            string hwid = d.substr(6);

            if (hwid.empty() || hwid.length() > 32) {
                logError("setup", "Invalid HWID length");
                return;
            }

            InlineKeyboardMarkup::Ptr kb(new InlineKeyboardMarkup);
            auto br = make_shared<InlineKeyboardButton>();
            br->text = "✏️ Изменить имя";
            br->callbackData = "ren_" + hwid;
            auto bd = make_shared<InlineKeyboardButton>();
            bd->text = "🗑 Удалить";
            bd->callbackData = "del_" + hwid;
            auto bb = make_shared<InlineKeyboardButton>();
            bb->text = "◀️ Назад";
            bb->callbackData = "back_to_profile";
            kb->inlineKeyboard = {{br, bd}, {bb}};
            try {
                bot.getApi().editMessageText(
                    "⚙️ <b>Управление устройством</b>\n<code>" + hwid + "</code>",
                    tgId, mid, "", "HTML", nullptr, kb);
            } catch (const exception &e) { logError("setup", e.what()); }
        } else if (d.rfind("del_", 0) == 0) {
            if (d.length() < 4) return;
            string hwid = d.substr(4);

            if (hwid.empty() || hwid.length() > 32) {
                logError("del", "Invalid HWID");
                return;
            }

            auto r = cpr::Post(cpr::Url{API_URL + "/auth/delete_device"},
                               cpr::Parameters{{"tg_id", to_string(tgId)}, {"hwid", hwid}},
                               cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                               cpr::Timeout{5000});

            if (r.status_code == 200) {
                try { bot.getApi().editMessageText("✅ Устройство отвязано.", tgId, mid, "", "HTML"); } catch (...) {
                }
            } else {
                logError("del", "Status: " + to_string(r.status_code));
                try { bot.getApi().answerCallbackQuery(q->id, "❌ Ошибка удаления", false, "", 3); } catch (...) {
                }
            }
        } else if (d.rfind("ren_", 0) == 0) {
            if (d.length() < 4) return;
            string hwid = d.substr(4);

            if (hwid.empty() || hwid.length() > 32) {
                logError("ren", "Invalid HWID");
                return;
            }

            std::lock_guard<std::mutex> lock(globalMutex);
            renamingDeviceId[tgId] = hwid;
            bot.getApi().sendMessage(tgId,
                                     "📝 Введите новое имя для <code>" + hwid.substr(0, 8) + "...</code>:",
                                     nullptr, nullptr, nullptr, "HTML");
        } else if (d.rfind("off_", 0) == 0) {
            if (d.length() < 4) return;
            string hwid = d.substr(4);
            bool hasPin = false;
            {
                std::lock_guard<std::mutex> lock(globalMutex);
                hasPin = userPins.count(tgId);
                if (hasPin) awaitingPin[tgId] = {hwid, "shutdown"};
            }
            if (hasPin)
                bot.getApi().sendMessage(tgId, "🔐 Введите пин для выключения:", nullptr, nullptr, nullptr, "HTML");
            else {
                cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                          cpr::Parameters{{"hwid", hwid}, {"command", "shutdown"}, {"tg_id", to_string(tgId)}},
                          cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                          cpr::Timeout{5000});
                bot.getApi().sendMessage(tgId, "🔌 Команда на выключение отправлена.");
            }
        } else if (d.rfind("revokecmd_", 0) == 0) {
            if (d.length() < 10) return;
            string hwid = d.substr(10);
            cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                      cpr::Parameters{
                          {"hwid", hwid}, {"command", "cancel_shutdown"}, {"tg_id", to_string(tgId)}
                      },
                      cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                      cpr::Timeout{5000});
            try {
                bot.getApi().answerCallbackQuery(q->id, "⏳ Отмена отправлена", true);
                bot.getApi().editMessageText("<b>✅ Выключение отозвано</b>", tgId, mid, "", "HTML");
            } catch (...) {
            }
        } else if (d.rfind("sleep_", 0) == 0) {
            if (d.length() < 6) return;
            string hwid = d.substr(6);
            bool hasPin = false;
            {
                std::lock_guard<std::mutex> lock(globalMutex);
                hasPin = userPins.count(tgId);
                if (hasPin) awaitingPin[tgId] = {hwid, "sleep"};
            }
            if (hasPin)
                bot.getApi().sendMessage(tgId, "🔐 Введите пин для сна:", nullptr, nullptr, nullptr, "HTML");
            else {
                cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                          cpr::Parameters{{"hwid", hwid}, {"command", "sleep"}, {"tg_id", to_string(tgId)}},
                          cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                          cpr::Timeout{5000});
                try { bot.getApi().answerCallbackQuery(q->id, "🌙 Сон отправлен", true); } catch (...) {
                }
            }
        } else if (d.rfind("cancel_sleep_", 0) == 0) {
            if (d.length() < 13) return;
            std::string hwid = d.substr(13);
            {
                std::lock_guard<std::mutex> lock(globalMutex);
                auto it = autoSleepSchedule.find(tgId);
                if (it != autoSleepSchedule.end()) {
                    hwid = it->second.first;
                    autoSleepSchedule.erase(it);
                }
            }
            if (!hwid.empty()) {
                auto r = cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                                   cpr::Parameters{
                                       {"hwid", hwid}, {"command", "cancel_shutdown"}, {"tg_id", to_string(tgId)}
                                   },
                                   cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                                   cpr::Timeout{5000});
                if (r.status_code == 200) {
                    try { bot.getApi().editMessageText("✅ Авто-сон отменён.", tgId, mid, "", "HTML"); } catch (...) {
                    }
                }
            }
        } else if (d.rfind("autosleep_set_", 0) == 0) {
            if (d.length() < 14) return;
            std::lock_guard<std::mutex> lock(globalMutex);
            awaitingAutoSleepTime[tgId] = d.substr(14);
            bot.getApi().sendMessage(tgId,
                                     "⏰ <b>Авто-сон</b>\n\n"
                                     "Введите время по МСК в формате <code>ЧЧ:ММ</code>\n"
                                     "Пример: <code>23:30</code>",
                                     nullptr, nullptr, nullptr, "HTML");
        } else if (d.rfind("autosleep_off_", 0) == 0) {
            if (d.length() < 14) return;
            {
                std::lock_guard<std::mutex> lock(globalMutex);
                autoSleepSchedule.erase(tgId);
            }
            try { bot.getApi().editMessageText("✅ Авто-сон отключён.", tgId, mid, "", "HTML"); } catch (...) {
            }
        } else if (d.rfind("scr_", 0) == 0) {
            if (d.length() < 4) return;
            string hwid = d.substr(4);
            bool hasPin = false;
            {
                std::lock_guard<std::mutex> lock(globalMutex);
                hasPin = userPins.count(tgId);
                if (hasPin) awaitingPin[tgId] = {hwid, "screenshot"};
            }
            if (hasPin)
                bot.getApi().sendMessage(tgId, "🔐 Введите пин для скриншота:", nullptr, nullptr, nullptr, "HTML");
            else {
                cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                          cpr::Parameters{{"hwid", hwid}, {"command", "screenshot"}, {"tg_id", to_string(tgId)}},
                          cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                          cpr::Timeout{5000});
                try { bot.getApi().answerCallbackQuery(q->id, "📸 Скриншот отправлен", true); } catch (...) {
                }
            }
        } else if (d.rfind("get_procs_", 0) == 0) {
            if (d.length() < 10) return;
            string th = d.substr(10);

            if (th.length() > 32 || th.empty()) {
                logError("get_procs", "Invalid hwid length");
                return;
            }

            auto r = cpr::Get(cpr::Url{API_URL + "/bot/status?tg_id=" + to_string(tgId)},
                              cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                              cpr::Timeout{5000});
            if (r.status_code != 200) return;
            try {
                auto j = nlohmann::json::parse(r.text);
                if (j.value("user_rank", "FREE") != "OVERSEER") {
                    bot.getApi().sendMessage(tgId, "❌ Только для OVERSEER.", nullptr, nullptr, nullptr, "HTML");
                    return;
                }

                bool found = false;
                for (auto &dev: j["devices"]) {
                    if (dev.value("hwid", "") != th) continue;
                    found = true;

                    if (!dev.contains("top_processes") || dev["top_processes"].empty()) {
                        bot.getApi().sendMessage(tgId, "⏳ Данные ещё не поступили.", nullptr, nullptr, nullptr, "HTML");
                        break;
                    }

                    string msg = "📊 <b>Топ процессов</b>\n──────────────────────────\n";
                    InlineKeyboardMarkup::Ptr pkb(new InlineKeyboardMarkup);

                    int procCount = 0;
                    for (auto &p: dev["top_processes"]) {
                        if (procCount++ > 20) break;

                        string pn = p.value("name", "?");
                        int pid = p.value("pid", 0);
                        float pc = p.value("cpu", 0.0f);
                        float pr = p.value("ram", 0.0f);

                        msg += "🔹 <b>" + pn + "</b>\n";
                        msg += "└ 💾 <code>" + to_string((int) pr) + "MB</code> | ⚡ <code>" + to_fixed(pc, 1) +
                                "%</code>\n";

                        auto bk = make_shared<InlineKeyboardButton>();
                        bk->text = "🛑 " + (pn.length() > 15 ? pn.substr(0, 12) + "..." : pn);
                        bk->callbackData = "kill_p:" + th + ":" + to_string(pid);
                        pkb->inlineKeyboard.push_back({bk});
                    }

                    auto backBtn = make_shared<InlineKeyboardButton>();
                    backBtn->text = "◀️ Назад";
                    backBtn->callbackData = "refresh_stats:" + th;
                    pkb->inlineKeyboard.push_back({backBtn});

                    if (msg.size() > 4000) msg = msg.substr(0, 3997) + "...";

                    bot.getApi().sendMessage(tgId, msg, nullptr, nullptr, pkb, "HTML");
                    break;
                }

                if (!found) {
                    logError("get_procs", "Device hwid not found: " + th);
                }
            } catch (const exception &e) { logError("get_procs", e.what()); }
        } else if (d.rfind("kill_p:", 0) == 0) {
            if (d.length() < 7) return;
            std::stringstream ss(d.substr(7));
            string hwid, pid;
            std::getline(ss, hwid, ':');
            std::getline(ss, pid, ':');

            if (hwid.empty() || pid.empty() ||
                hwid.length() > 32 || pid.length() > 10 ||
                !std::all_of(pid.begin(), pid.end(), ::isdigit)) {
                logError("kill_p", "Invalid parameters");
                return;
            }

            int pidVal = 0;
            if (!safeStoi(pid, pidVal)) {
                logError("kill_p", "PID conversion failed");
                return;
            }

            if (pidVal < 0) {
                logError("kill_p", "PID is negative");
                return;
            }

            auto r = cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                               cpr::Parameters{{"hwid", hwid}, {"command", "kill_" + pid}, {"tg_id", to_string(tgId)}},
                               cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                               cpr::Timeout{5000});

            if (r.status_code == 200) {
                try { bot.getApi().answerCallbackQuery(q->id, "✅ Kill PID " + pid); } catch (...) {
                }
            } else {
                logError("kill_p", "Status: " + to_string(r.status_code));
            }
        } else if (d.rfind("mute_1h_", 0) == 0) {
            if (d.length() < 8) return;
            string hwid = d.substr(8);
            auto r = cpr::Post(cpr::Url{API_URL + "/bot/mute"},
                               cpr::Parameters{{"hwid", hwid}, {"hours", "1"}, {"tg_id", to_string(tgId)}},
                               cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                               cpr::Timeout{5000});
            if (r.status_code == 200)
                try {
                    bot.getApi().answerCallbackQuery(q->id, "🔇 Пауза 1 час");
                    bot.getApi().editMessageText("🔔 <i>Алерты на паузе 1 час.</i>", tgId, mid, "", "HTML");
                } catch (...) {
                }
        } else if (d.rfind("clear_stats_", 0) == 0) {
            if (d.length() < 12) return;
            string hwid = d.substr(12);
            auto r = cpr::Post(cpr::Url{API_URL + "/bot/clear_stats"},
                               cpr::Parameters{{"hwid", hwid}, {"tg_id", to_string(tgId)}},
                               cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                               cpr::Timeout{5000});
            if (r.status_code == 200)
                try {
                    bot.getApi().editMessageText("🗑 <b>Статистика очищена.</b>", tgId, mid, "", "HTML");
                } catch (...) {
                }
        } else if (d == "delete_msg") {
            try { bot.getApi().deleteMessage(tgId, mid); } catch (...) {
            }
        } else if (d == "buy_sentinel" || d == "buy_overseer") {
            string rank = (d == "buy_sentinel") ? "SENTINEL" : "OVERSEER";
            auto r = cpr::Post(
                cpr::Url{API_URL + "/create_payment"},
                cpr::Parameters{{"tg_id", to_string(tgId)}, {"rank", rank}},
                cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                cpr::Timeout{5000}
            );
            if (r.status_code == 200) {
                try {
                    auto j = nlohmann::json::parse(r.text);
                    string pay_url = j["payment_url"];
                    string payment_id = j["payment_id"];

                    {
                        std::lock_guard<std::mutex> lock(globalMutex);
                        awaitingPaymentId[tgId] = payment_id;
                        awaitingPaymentRank[tgId] = rank;
                    }

                    InlineKeyboardMarkup::Ptr kb(new InlineKeyboardMarkup);
                    auto btn = make_shared<InlineKeyboardButton>();
                    btn->text = "💳 Оплатить картой";
                    btn->url = pay_url;

                    auto btnCheck = make_shared<InlineKeyboardButton>();
                    btnCheck->text = "✅ Я оплатил";
                    btnCheck->callbackData = "check_payment";

                    kb->inlineKeyboard.push_back({btn});
                    kb->inlineKeyboard.push_back({btnCheck});

                    bot.getApi().sendMessage(tgId,
                                             "💳 <b>Оплата " + rank + "</b>\n\n"
                                             "Перейди по ссылке, оплати,\n"
                                             "затем нажми «✅ Я оплатил»",
                                             nullptr, nullptr, kb, "HTML");
                } catch (const exception &e) {
                    logError("buy_payment", e.what());
                    bot.getApi().sendMessage(tgId, "❌ Ошибка создания платежа.");
                }
            } else {
                bot.getApi().sendMessage(tgId, "❌ Ошибка создания платежа. Попробуйте позже.");
            }
        } else if (d == "trial_overseer") {
            auto r = cpr::Post(cpr::Url{API_URL + "/bot/activate_trial"},
                               cpr::Parameters{{"tg_id", to_string(tgId)}, {"rank", "OVERSEER"}, {"days", "7"}},
                               cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                               cpr::Timeout{5000});
            if (r.status_code == 200)
                bot.getApi().sendMessage(tgId,
                                         "🎁 <b>Пробный OVERSEER на 7 дней активирован!</b> 👑\n\n"
                                         "Теперь у тебя есть:\n"
                                         "• 📸 Скриншоты\n"
                                         "• 📊 Мониторинг процессов\n"
                                         "• ⏰ Авто-сон по расписанию\n"
                                         "• 10 устройств\n\n"
                                         "Привяжи ПК: /download",
                                         nullptr, nullptr, nullptr, "HTML");
            else if (r.status_code == 409)
                bot.getApi().sendMessage(tgId, "❌ Пробный период уже был использован.", nullptr, nullptr, nullptr,
                                         "HTML");
            else
                bot.getApi().sendMessage(tgId, "❌ Ошибка активации.", nullptr, nullptr, nullptr, "HTML");
        } else if (d.rfind("health_", 0) == 0) {
            auto r = cpr::Get(cpr::Url{API_URL + "/bot/status?tg_id=" + to_string(tgId)},
                              cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                              cpr::Timeout{5000});
            if (r.status_code != 200) return;
            try {
                auto j = nlohmann::json::parse(r.text);
                string rank = j.value("user_rank", "FREE");

                if (rank == "FREE") {
                    bot.getApi().sendMessage(tgId,
                                             "🔒 <b>Состояние ПК</b>\n\n"
                                             "Доступно только для подписчиков.\n\n"
                                             "📊 <b>Что получите:</b>\n"
                                             "• Температуры за 7 дней\n"
                                             "• Диагностика дисков\n"
                                             "• Пиковые нагрузки\n"
                                             "• Рекомендации\n\n"
                                             "👉 /subscribe",
                                             nullptr, nullptr, nullptr, "HTML");
                    return;
                }

                string th;
                if (d.rfind("health_select:", 0) == 0) {
                    if (d.length() < 14) return;
                    th = d.substr(14);
                } else {
                    if (d.length() < 7) return;
                    th = d.substr(7);
                }

                auto devs = j.contains("devices") ? j["devices"] : nlohmann::json::array();
                if (devs.empty()) {
                    bot.getApi().sendMessage(tgId, "📡 Устройства не найдены.");
                    return;
                }

                if (th.empty()) {
                    if (devs.size() == 1) {
                        th = devs[0].value("hwid", "");
                    } else {
                        InlineKeyboardMarkup::Ptr kb(new InlineKeyboardMarkup);
                        for (auto &dev: devs) {
                            auto b = make_shared<InlineKeyboardButton>();
                            b->text = (dev.value("online", false) ? "🟢 " : "🔴 ") + dev.value("name", "PC");
                            b->callbackData = "health_select:" + dev.value("hwid", "");
                            kb->inlineKeyboard.push_back({b});
                        }
                        bot.getApi().sendMessage(tgId, "🏥 <b>Выберите устройство:</b>", nullptr, nullptr, kb, "HTML");
                        return;
                    }
                }
                if (th.empty()) return;

                auto hr = cpr::Get(
                    cpr::Url{API_URL + "/bot/status/" + th + "?tg_id=" + to_string(tgId)},
                    cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                    cpr::Timeout{5000});
                if (hr.status_code != 200) {
                    bot.getApi().sendMessage(tgId, "❌ Ошибка получения данных.");
                    return;
                }

                auto hj = nlohmann::json::parse(hr.text);
                string st = hj.value("status", "");

                if (st == "no_data") {
                    bot.getApi().sendMessage(tgId,
                                             "⏳ <b>Данных пока нет.</b>\n\nЗайдите через несколько часов — система уже собирает данные.",
                                             nullptr, nullptr, nullptr, "HTML");
                    return;
                }

                if (st == "ready") {
                    float cta = hj.value("cpu_temp_avg", 0.0f);
                    float ctm = hj.value("cpu_temp_max", 0.0f);
                    float cla = hj.value("cpu_load_avg", 0.0f);
                    float rla = hj.value("ram_load_avg", 0.0f);

                    string ts = ctm > 85 ? "🔴 КРИТИЧНО" : ctm > 75 ? "🟡 ВНИМАНИЕ" : "🟢 НОРМА";
                    string vd = ctm > 85
                                    ? "⚠️ Требуется чистка/замена термопасты"
                                    : ctm > 75
                                          ? "🔧 Проверьте охлаждение"
                                          : "✅ ПК работает в штатном режиме";

                    stringstream ss;
                    ss << "🏥 <b>ДИАГНОСТИКА ПК — " << hj.value("agent_name", "Device") << "</b>\n";
                    ss << "📅 С <b>" << hj.value("period_from", "N/A") << "</b> по <b>" << hj.value("period_to", "N/A")
                            << "</b>\n";
                    ss << "━━━━━━━━━━━━━━━━━━━━━━━━\n\n";

                    ss << "🖥 <b>Процессор:</b> <code>" << hj.value("cpu_name", "N/A") << "</code>\n";
                    ss << "🎮 <b>Видеокарта:</b> <code>" << hj.value("gpu_name", "N/A") << "</code>\n\n";

                    ss << "🌡 <b>Температуры CPU:</b>\n";
                    ss << "├ Средняя: <code>" << to_fixed(cta, 1) << "°C</code>\n";
                    ss << "├ Пиковая: <code>" << to_fixed(ctm, 1) << "°C</code>";
                    ss << " (" << hj.value("peak_temp_time", "N/A") << ")\n";
                    ss << "└ Статус: " << ts << "\n\n";

                    ss << "📊 <b>Средняя нагрузка:</b>\n";
                    ss << "├ CPU: <code>" << to_fixed(cla, 1) << "%</code>\n";
                    ss << "└ RAM: <code>" << to_fixed(rla, 1) << "%</code>\n\n";

                    ss << "⏱ <b>Онлайн за период:</b> <code>" << hj.value("online_str", "N/A") << "</code>\n\n";

                    if (hj.contains("disks") && !hj["disks"].empty()) {
                        ss << "💾 <b>Состояние дисков:</b>\n";
                        for (auto &dd: hj["disks"]) {
                            float total = dd.value("total", 0.0f);
                            float used = dd.value("used", 0.0f);
                            ss << "├ <b>" << dd.value("name", "?") << ":</b>\n";
                            ss << "│ Занято: <code>" << to_fixed(used, 1) << "/" << to_fixed(total, 1)
                                    << " GB (" << (int) dd.value("percent", 0.0f) << "%)</code>\n";
                            ss << "└ " << dd.value("wear_status", "🟢 Норма") << "\n\n";
                        }
                    }

                    ss << "⚡ <b>Энергопотребление:</b>\n";
                    if (hj.contains("estimated_kwh") && !hj["estimated_kwh"].is_null()) {
                        if (rank == "OVERSEER") {
                            ss << "├ МОЩНОСТЬ БП: <code>" << hj.value("psu_watts", 0) << "W</code>\n";
                        }
                        ss << "├ Потреблено: <code>" << to_fixed(hj.value("estimated_kwh", 0.0f), 2) <<
                                " кВт/ч</code>\n";
                        ss << "└ Стоимость: <code>~" << to_fixed(hj.value("estimated_cost", 0.0f), 1) <<
                                " ₽</code>\n\n";
                    } else {
                        ss << "└ не определено — <code>/psu [ватты]</code>\n\n";
                    }

                    if (rank == "OVERSEER") {
                        ss << "🎮 <b>Видеокарта за период:</b>\n";
                        ss << "├ Ср. нагрузка: <code>" << to_fixed(hj.value("gpu_load_avg", 0.0f), 1) << "%</code>\n";
                        ss << "├ Ср. темп: <code>" << to_fixed(hj.value("gpu_temp_avg", 0.0f), 1) << "°C</code>\n";
                        ss << "└ Макс. темп: <code>" << to_fixed(hj.value("gpu_temp_max", 0.0f), 1) << "°C</code>\n\n";

                        ss << "🌐 <b>Сеть за период:</b>\n";
                        ss << "├ Ср. входящий: <code>" << to_fixed(hj.value("net_down_avg", 0.0f), 2) <<
                                " MB/s</code>\n";
                        ss << "└ Ср. исходящий: <code>" << to_fixed(hj.value("net_up_avg", 0.0f), 2) <<
                                " MB/s</code>\n\n";

                        if (hj.contains("top_processes_week") && !hj["top_processes_week"].empty()) {
                            ss << "📊 <b>Топ процессов за период:</b>\n";

                            int procCount = 0;
                            for (auto &p: hj["top_processes_week"]) {
                                if (procCount++ >= 10) break;

                                string pName = p.value("name", "?");
                                float pCpu = p.value("avg_cpu", 0.0f);
                                float pRam = p.value("ram_usage", 0.0f);

                                string prefix = (procCount == (int) hj["top_processes_week"].size()) ? "└ " : "├ ";

                                string ramStr;
                                if (pRam >= 1024) {
                                    ramStr = to_fixed(pRam / 1024, 1) + " GB";
                                } else {
                                    ramStr = to_string((int) pRam) + " MB";
                                }

                                ss << prefix << "<code>" << pName << "</code> — "
                                        << to_fixed(pCpu, 1) << "% CPU"
                                        << " | 💾 " << ramStr << "\n";
                            }
                            ss << "\n";
                        }
                    }

                    ss << "━━━━━━━━━━━━━━━━━━━━━━━━\n";
                    ss << "🔮 <b>Итог:</b> " << vd;

                    string msgStr = ss.str();
                    if (msgStr.size() > 4000) msgStr = msgStr.substr(0, 3997) + "...";

                    InlineKeyboardMarkup::Ptr kb(new InlineKeyboardMarkup);
                    auto brf = make_shared<InlineKeyboardButton>();
                    brf->text = "🔄 Обновить";
                    brf->callbackData = "health_select:" + th;
                    kb->inlineKeyboard.push_back({brf});

                    if (devs.size() > 1) {
                        auto bother = make_shared<InlineKeyboardButton>();
                        bother->text = "◀️ Другой ПК";
                        bother->callbackData = "health_";
                        kb->inlineKeyboard.push_back({bother});
                    }

                    if (rank == "OVERSEER") {
                        auto bc = make_shared<InlineKeyboardButton>();
                        bc->text = "🗑 Очистить статистику";
                        bc->callbackData = "clear_stats_" + th;
                        kb->inlineKeyboard.push_back({bc});
                    }

                    bot.getApi().sendMessage(tgId, msgStr, nullptr, nullptr, kb, "HTML");
                }
            } catch (const exception &e) { logError("health", e.what()); }
        } else if (d == "psu_skip") {
            cpr::Post(cpr::Url{API_URL + "/bot/set_psu"},
                      cpr::Parameters{{"tg_id", to_string(tgId)}, {"watts", "250"}},
                      cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                      cpr::Timeout{5000});
            bot.getApi().sendMessage(tgId,
                                     "✅ Установлена заглушка <b>250W</b>.\nИзменить: <code>/psu [ватты]</code>",
                                     nullptr, nullptr, nullptr, "HTML");
            showMesStatus(bot, tgId);
        } else if (d == "check_payment") {
            string payment_id, rank;
            {
                std::lock_guard<std::mutex> lock(globalMutex);
                auto it1 = awaitingPaymentId.find(tgId);
                auto it2 = awaitingPaymentRank.find(tgId);
                if (it1 == awaitingPaymentId.end() || it2 == awaitingPaymentRank.end()) {
                    bot.getApi().sendMessage(tgId, "❌ Нет активного платежа. Нажмите 'Купить' снова.");
                    return;
                }
                payment_id = it1->second;
                rank = it2->second;
            }

            const char *shop_id_env = std::getenv("YOOKASSA_SHOP_ID");
            const char *secret_key_env = std::getenv("YOOKASSA_SECRET_KEY");

            if (!shop_id_env || !secret_key_env) {
                bot.getApi().sendMessage(tgId, "❌ Ошибка конфигурации сервера.");
                return;
            }

            string auth_b64 = base64_encode(string(shop_id_env) + ":" + string(secret_key_env));

            auto r = cpr::Get(
                cpr::Url{"https://api.yookassa.ru/v3/payments/" + payment_id},
                cpr::Header{
                    {"Authorization", "Basic " + auth_b64},
                    {"Content-Type", "application/json"}
                },
                cpr::Timeout{10000}
            );

            if (r.status_code == 200) {
                try {
                    auto j = nlohmann::json::parse(r.text);
                    string status = j.value("status", "");

                    if (status == "succeeded") {
                        const char *api_token_env = std::getenv("NEUROGUARD_INTERNAL_API_TOKEN");
                        if (!api_token_env) {
                            bot.getApi().sendMessage(tgId, "❌ Ошибка: нет internal token.");
                            return;
                        }
                        auto upd = cpr::Post(
                            cpr::Url{API_URL + "/bot/upgrade"},
                            cpr::Payload{
                                {"tg_id", to_string(tgId)},
                                {"rank", rank},
                                {"days", "30"},
                                {"api_token", string(api_token_env)}
                            },
                            cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                            cpr::Timeout{5000}
                        );
                        if (upd.status_code == 200) {
                            std::lock_guard<std::mutex> lock(globalMutex);
                            awaitingPaymentId.erase(tgId);
                            awaitingPaymentRank.erase(tgId);
                            string emoji = (rank == "OVERSEER") ? "👑" : "🛡";
                            bot.getApi().sendMessage(tgId,
                                                     emoji + " <b>Подписка " + rank + " активирована!</b>\n\n"
                                                     "Используй /download чтобы получить агента.",
                                                     nullptr, nullptr, nullptr, "HTML");
                        } else {
                            bot.getApi().sendMessage(tgId, "❌ Ошибка активации. Напишите @TLEET_BLANT");
                        }
                    } else if (status == "pending" || status == "waiting_for_capture") {
                        bot.getApi().sendMessage(tgId, "⏳ Платёж обрабатывается. Подождите минуту и попробуйте снова.");
                    } else {
                        bot.getApi().sendMessage(
                            tgId, "❌ Платёж не прошёл (статус: " + status + "). Попробуйте ещё раз.");
                    }
                } catch (const exception &e) {
                    logError("check_payment parse", e.what());
                    bot.getApi().sendMessage(tgId, "❌ Ошибка обработки ответа.");
                }
            } else if (r.status_code == 404) {
                bot.getApi().sendMessage(tgId, "❌ Платёж не найден. Попробуйте создать новый.");
            } else {
                logError("check_payment", "Status: " + to_string(r.status_code) + " body: " + r.text);
                bot.getApi().sendMessage(tgId, "❌ Ошибка проверки платежа (" + to_string(r.status_code) + ").");
            }
        }
    });

    bot.getEvents().onAnyMessage([&](Message::Ptr m) {
        long long tgId = m->chat->id;
        string text = m->text;
        if (text.empty() || text[0] == '/') return;
        if (!checkRateLimit(tgId, 800)) return;

        {
            std::lock_guard<std::mutex> lock(globalMutex);

            if (awaitingSetPin.count(tgId)) {
                awaitingSetPin.erase(tgId);
                if (text.size() == 4 && std::all_of(text.begin(), text.end(), ::isdigit)) {
                    userPins[tgId] = text;
                    bot.getApi().sendMessage(tgId,
                                             "✅ <b>Пин-код установлен!</b>\n\n"
                                             "Удалить: /removepin",
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
                    bot.getApi().sendMessage(tgId,
                                             "🔓 <b>Пин-код удалён.</b>",
                                             nullptr, nullptr, nullptr, "HTML");
                } else {
                    bot.getApi().sendMessage(tgId,
                                             "❌ Неверный пин. Удаление отменено.",
                                             nullptr, nullptr, nullptr, "HTML");
                }
                return;
            }

            if (awaitingPin.count(tgId)) {
                try {
                    auto [hwid, cmd] = awaitingPin[tgId];
                    awaitingPin.erase(tgId);

                    if (userPins.count(tgId) && userPins[tgId] == text) {
                        try {
                            auto r = cpr::Post(cpr::Url{API_URL + "/bot/set_command"},
                                               cpr::Parameters{
                                                   {"hwid", hwid}, {"command", cmd}, {"tg_id", to_string(tgId)}
                                               },
                                               cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                                               cpr::Timeout{5000});

                            if (r.status_code == 200) {
                                bot.getApi().sendMessage(tgId,
                                                         "✅ Команда <b>" + cmd + "</b> отправлена.",
                                                         nullptr, nullptr, nullptr, "HTML");
                            } else {
                                logError("pin command", "Status: " + to_string(r.status_code));
                                bot.getApi().sendMessage(tgId, "❌ Ошибка отправки команды.", nullptr, nullptr, nullptr,
                                                         "HTML");
                            }
                        } catch (const exception &e) {
                            logError("pin command send", e.what());
                            bot.getApi().sendMessage(tgId, "❌ Ошибка соединения.", nullptr, nullptr, nullptr, "HTML");
                        }
                    } else {
                        bot.getApi().sendMessage(tgId, "❌ Неверный пин. Отменено.", nullptr, nullptr, nullptr, "HTML");
                    }
                    return;
                } catch (const exception &e) {
                    logError("awaitingPin", e.what());
                    awaitingPin.erase(tgId);
                    return;
                }
            }

            if (awaitingAutoSleepTime.count(tgId)) {
                string hwid = awaitingAutoSleepTime[tgId];
                awaitingAutoSleepTime.erase(tgId);
                auto [h, mn] = parseTime(text);
                if (h >= 0) {
                    autoSleepSchedule[tgId] = {hwid, text};
                    bot.getApi().sendMessage(tgId,
                                             "✅ <b>Авто-сон настроен: " + text + " МСК</b>\n\n"
                                             "ПК будет уходить в сон каждый день в это время.\n"
                                             "Отключить: нажми кнопку <b>⏰ Авто-сон: ВКЛ</b> в статусе устройства.",
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
            if (fileExists(PHOTO_MAIN)) {
                sendPhoto(bot, tgId, PHOTO_MAIN, captionText, mainMenu);
            } else {
                logError("main menu", "File not found: " + PHOTO_MAIN);
                bot.getApi().sendMessage(tgId, captionText, nullptr, nullptr, mainMenu, "HTML");
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lock(globalMutex);

            if (awaitingName.count(tgId)) {
                awaitingName.erase(tgId);

                if (text.empty() || text.length() > 64) {
                    bot.getApi().sendMessage(tgId,
                                             "❌ Имя должно быть 1-64 символа.",
                                             nullptr, nullptr, nullptr, "HTML");
                    return;
                }

                string sanitized = text;
                sanitized.erase(remove_if(sanitized.begin(), sanitized.end(),
                                          [](unsigned char c) { return c < 32 || c == '"' || c == '\''; }),
                                sanitized.end());

                try {
                    auto r = cpr::Post(cpr::Url{API_URL + "/auth/update_name"},
                                       cpr::Parameters{{"tg_id", to_string(tgId)}, {"name", sanitized}},
                                       cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                                       cpr::Timeout{5000});

                    if (r.status_code == 200) {
                        bot.getApi().sendMessage(tgId,
                                                 "✅ ПК назван: <b>" + sanitized +
                                                 "</b>\n\nПроверьте состояние через 📊 Статус.",
                                                 nullptr, nullptr, mainMenu, "HTML");
                    } else {
                        logError("update_name", "Status: " + to_string(r.status_code));
                        bot.getApi().sendMessage(tgId,
                                                 "❌ Ошибка сохранения. Попробуйте позже.",
                                                 nullptr, nullptr, nullptr, "HTML");
                    }
                } catch (const exception &e) {
                    logError("update_name", e.what());
                    bot.getApi().sendMessage(tgId,
                                             "❌ Ошибка соединения.",
                                             nullptr, nullptr, nullptr, "HTML");
                }
                return;
            }

            if (renamingDeviceId.count(tgId)) {
                string th = renamingDeviceId[tgId];
                renamingDeviceId.erase(tgId);

                if (text.empty() || text.length() > 64) {
                    bot.getApi().sendMessage(tgId,
                                             "❌ Имя должно быть 1-64 символа.",
                                             nullptr, nullptr, nullptr, "HTML");
                    return;
                }

                if (th.empty() || th.length() > 32 ||
                    !std::all_of(th.begin(), th.end(), [](unsigned char c) {
                        return std::isalnum(c) || c == '-';
                    })) {
                    logError("rename device", "Invalid HWID: " + th);
                    bot.getApi().sendMessage(tgId,
                                             "❌ Ошибка: некорректный ID устройства.",
                                             nullptr, nullptr, nullptr, "HTML");
                    return;
                }

                string sanitized = text;
                sanitized.erase(remove_if(sanitized.begin(), sanitized.end(),
                                          [](unsigned char c) { return c < 32 || c == '"' || c == '\''; }),
                                sanitized.end());

                try {
                    auto r = cpr::Post(cpr::Url{API_URL + "/auth/update_name"},
                                       cpr::Parameters{{"tg_id", to_string(tgId)}, {"name", sanitized}, {"hwid", th}},
                                       cpr::Header{{"ngrok-skip-browser-warning", "true"}},
                                       cpr::Timeout{5000});

                    if (r.status_code == 200) {
                        bot.getApi().sendMessage(tgId,
                                                 "✅ Имя изменено: <b>" + sanitized + "</b>",
                                                 nullptr, nullptr, mainMenu, "HTML");
                    } else {
                        logError("rename device", "Status: " + to_string(r.status_code));
                        bot.getApi().sendMessage(tgId,
                                                 "❌ Ошибка сохранения. Попробуйте позже.",
                                                 nullptr, nullptr, nullptr, "HTML");
                    }
                } catch (const exception &e) {
                    logError("rename device", e.what());
                    bot.getApi().sendMessage(tgId,
                                             "❌ Ошибка соединения.",
                                             nullptr, nullptr, nullptr, "HTML");
                }
                return;
            }
        }

        if (text == "📊 Статус") showMesStatus(bot, tgId);
        else if (text == "🆘 Поддержка") {
            if (fileExists(PHOTO_SUPP)) {
                sendPhoto(bot, tgId, PHOTO_SUPP, support, backMenu);
            } else {
                logError("support", "File not found: " + PHOTO_SUPP);
                bot.getApi().sendMessage(tgId, support, nullptr, nullptr, backMenu, "HTML");
            }
        } else if (text == "ℹ️ FAQ") {
            if (fileExists(PHOTO_FAQ)) {
                bot.getApi().sendPhoto(tgId,
                                       TgBot::InputFile::fromFile(PHOTO_FAQ, "image/png"),
                                       about, 0, backMenu, "HTML");
            } else {
                logError("FAQ", "File not found: " + PHOTO_FAQ);
                bot.getApi().sendMessage(tgId, about, nullptr, nullptr, backMenu, "HTML");
            }
        } else if (text == "👤 Профиль") showMesProfile(bot, m);
        else if (text == "💎 Подписка") {
            if (fileExists(PHOTO_SUB)) {
                bot.getApi().sendPhoto(tgId,
                                       TgBot::InputFile::fromFile(PHOTO_SUB, "image/png"),
                                       subCaption, 0, subKeyboard, "HTML");
            } else {
                logError("subscription", "File not found: " + PHOTO_SUB);
                bot.getApi().sendMessage(tgId, subCaption, nullptr, nullptr, subKeyboard, "HTML");
            }
        }
    });

    std::signal(SIGINT, [](int) {
        logInfo("signal", "SIGINT received, shutting down...");
        running = false;
    });

    std::signal(SIGTERM, [](int) {
        logInfo("signal", "SIGTERM received, shutting down...");
        running = false;
    });
    adminPanel.registerHandlers();

    logInfo("main", "Polling started");
    int reconnectAttempts = 0;
    const int MAX_RECONNECT_ATTEMPTS = 5;
    const int RECONNECT_DELAY_MS = 3000;

    while (running) {
        try {
            reconnectAttempts = 0;
            TgLongPoll lp(bot);
            while (running) {
                try {
                    lp.start();
                } catch (const std::exception &pollError) {
                    logError("polling", pollError.what());
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    if (++reconnectAttempts > MAX_RECONNECT_ATTEMPTS) {
                        logError("polling", "Max reconnect attempts reached");
                        throw;
                    }
                }
            }
        } catch (const exception &e) {
            logError("main", e.what());
            if (running) {
                logInfo("main", "Reconnecting in " + to_string(RECONNECT_DELAY_MS) + "ms...");
                std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY_MS));
            }
        }
    }

    logInfo("main", "Waiting for threads to finish...");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    logInfo("main", "Bot stopped");
    return 0;
}
