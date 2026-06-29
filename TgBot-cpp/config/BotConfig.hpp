#pragma once
// ═══════════════════════════════════════════════════════════
//  BotConfig.hpp
//  Константы, URL, пути к фото, тексты
// ═══════════════════════════════════════════════════════════

#include <string>

// ── API ─────────────────────────────────────────────────────
inline const std::string API_URL       = "U_API";
inline const std::string NEWS_CHANNEL  = "https://t.me/NeuroGuardNews";
inline const std::string EULA_URL      = "https://docs.google.com/document/d/1l-FpslFh7d-ICGmJX8UegVe6m-9UuAlMvGWUEGL_ai8/edit?usp=sharing";

// ── Фото ────────────────────────────────────────────────────
inline const std::string PHOTO_MAIN    = "Images/MainMenu-2.png";
inline const std::string PHOTO_SUB     = "Images/Subscription.png";
inline const std::string PHOTO_SUPP    = "Images/Support.png";
inline const std::string PHOTO_FAQ     = "Images/FAQ.png";
inline const std::string PHOTO_PROFILE = "Images/PROFILE.png";

// ── Тексты ──────────────────────────────────────────────────
inline const std::string TEXT_WELCOME =
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
    "<a href='https://docs.google.com/document/d/1l-FpslFh7d-ICGmJX8UegVe6m-9UuAlMvGWUEGL_ai8/edit?usp=sharing'>правилами использования</a></i>\n\n"
    "<i>⚡️ NeuroGuard — твоё железо под защитой 🛡</i>";

inline const std::string TEXT_DOWNLOAD =
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
    "Некоторые антивирусы могут ложно сработать на драйвер WinRing0.sys.\n\n"
    "<b>Это НЕ вирус и НЕ RAT.</b>\n\n"
    "<code>━━━━━━━━━━━━━━━━━━━━━━━━</code>\n\n"
    "<b>🛠 Инструкция:</b>\n"
    "1️⃣ Скачайте архив и распакуйте\n"
    "2️⃣ Запустите <code>NeuroGuard_Setup.exe</code> от администратора\n"
    "3️⃣ Скопируйте код из окна программы\n"
    "4️⃣ Отправьте боту: <code>/auth ВАШ_КОД</code>\n\n"
    "<b>❓ Проблемы:</b> ✉️ @TLEET_BLANT";

inline const std::string TEXT_SUPPORT =
    "<b>🛡 NeuroGuard Support</b>\n"
    "━━━━━━━━━━━━━━━━━━━━━━━━\n"
    "👋 <b>Привет!</b> Всегда на связи!\n\n"
    "🚀 <b>Текущая фаза:</b> <code>Public Beta v1.0</code>\n\n"
    "<b>Чем помогаем:</b>\n"
    "🔹 Настройка и исключения антивируса\n"
    "🔹 Разбор данных нагрузки и температур\n"
    "🔹 Вылеты и проблемы с соединением\n\n"
    "✉️ <b>Разработчик:</b> @TLEET_BLANT\n"
    "📢 <b>Новости:</b> <a href='https://t.me/NeuroGuardNews'>@NeuroGuardNews</a>\n\n"
    "<i>Спасибо, что помогаешь делать NeuroGuard лучше! 🛠</i>";

inline const std::string TEXT_ABOUT =
    "<b>🛡 NeuroGuard: Контроль и Безопасность</b>\n"
    "━━━━━━━━━━━━━━━━━━━━━━━━\n\n"
    "🔹 <b>Безопасность:</b> VPS в Нидерландах, шифрование данных.\n\n"
    "🔹 <b>Анти-шпионаж:</b> При скриншоте на ПК всегда Alert-уведомление.\n\n"
    "🔹 <b>HWID и Лицензия:</b> Подписка привязана к оборудованию.\n\n"
    "✉️ <b>Связь:</b> @TLEET_BLANT\n"
    "📢 <a href='https://t.me/NeuroGuardNews'>@NeuroGuardNews</a>\n"
    "<i>v1.0 Public Beta | NeuroGuard Team</i>";

inline const std::string TEXT_SUB_CAPTION =
    "<b>🚀 Тарифы NeuroGuard</b>\n\n"
    "🆓 <b>FREE</b> — 1 ПК, только мониторинг\n"
    "🛡 <b>SENTINEL</b> — 3 ПК, 299₽/мес, управление\n"
    "👑 <b>OVERSEER</b> — 10 ПК, 499₽/мес, скриншоты, процессы\n\n"
    "🎁 <b>Пробный период 7 дней OVERSEER</b> — для новых!";

inline const std::string TEXT_MAIN_CAPTION =
    "<b>▋ ГЛАВНОЕ МЕНЮ</b>\n"
    "──────────────────\n"
    "<i>Выберите раздел.</i>";