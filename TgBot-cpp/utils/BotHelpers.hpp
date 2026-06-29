#pragma once
// ═══════════════════════════════════════════════════════════
//  BotHelpers.hpp
//  Утилиты: логи, rate limit, sendPhoto, парсинг
// ═══════════════════════════════════════════════════════════

#include <string>
#include <iostream>
#include <chrono>
#include <mutex>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <tgbot/tgbot.h>
#include "../state/BotState.hpp"

// ── Логи ────────────────────────────────────────────────────
inline void logError(const std::string& ctx, const std::string& err) {
    std::cerr << "[ERROR] " << ctx << ": " << err << std::endl;
}

inline void logInfo(const std::string& ctx, const std::string& msg) {
    std::cout << "[INFO] " << ctx << ": " << msg << std::endl;
}

// ── Файловые утилиты ─────────────────────────────────────────
inline bool fileExists(const std::string& path) {
    return std::filesystem::exists(path);
}

// ── Rate limit ───────────────────────────────────────────────
inline bool checkRateLimit(long long tgId, int ms = 1500) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(globalMutex);
    auto it = lastActionTime.find(tgId);
    if (it != lastActionTime.end() &&
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second).count() < ms)
        return false;
    lastActionTime[tgId] = now;
    return true;
}

// ── Форматирование ───────────────────────────────────────────
inline std::string toFixed(float v, int p) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(p) << v;
    return o.str();
}

inline std::string base64Encode(const std::string& in) {
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
                [(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
            [((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

inline std::pair<int,int> parseTime(const std::string& s) {
    if (s.size() != 5 || s[2] != ':') return {-1, -1};
    try {
        int h = std::stoi(s.substr(0, 2));
        int m = std::stoi(s.substr(3, 2));
        if (h < 0 || h > 23 || m < 0 || m > 59) return {-1, -1};
        return {h, m};
    } catch (...) { return {-1, -1}; }
}

inline bool safeStoi(const std::string& str, int& result) {
    try {
        long long val = std::stoll(str);
        if (val < std::numeric_limits<int>::min() ||
            val > std::numeric_limits<int>::max()) return false;
        result = (int)val;
        return true;
    } catch (...) { return false; }
}

// ── Telegram утилиты ─────────────────────────────────────────
inline TgBot::KeyboardButton::Ptr makeButton(const std::string& t) {
    auto b = std::make_shared<TgBot::KeyboardButton>();
    b->text = t;
    return b;
}

inline void sendPhoto(TgBot::Bot& bot, long long cid,
                      const std::string& path, const std::string& cap,
                      TgBot::GenericReply::Ptr kb = nullptr) {
    try {
        if (!fileExists(path)) {
            logError("sendPhoto", "File not found: " + path);
            bot.getApi().sendMessage(cid, cap, nullptr, nullptr, kb, "HTML");
            return;
        }
        bot.getApi().sendPhoto(
            cid, TgBot::InputFile::fromFile(path, "image/png"),
            cap, 0, kb, "HTML");
    } catch (const std::exception& e) {
        logError("sendPhoto", e.what());
        try {
            bot.getApi().sendMessage(cid, cap, nullptr, nullptr, kb, "HTML");
        } catch (const std::exception& e2) {
            logError("sendPhoto fallback", e2.what());
        }
    }
}