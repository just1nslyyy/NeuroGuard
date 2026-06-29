#pragma once
// ═══════════════════════════════════════════════════════════
//  BotState.hpp
//  Глобальные стейты бота — maps, mutex, atomic
// ═══════════════════════════════════════════════════════════

#include <string>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include "../subscription/ISubscription.hpp"

// ── Глобальный mutex ─────────────────────────────────────────
inline std::mutex globalMutex;

// ── Флаг работы бота ─────────────────────────────────────────
inline std::atomic<bool> running(true);

// ── Платежи ──────────────────────────────────────────────────
inline std::map<long long, std::string> awaitingPaymentId;
inline std::map<long long, std::string> awaitingPaymentRank;

// ── Ввод имени устройства ────────────────────────────────────
inline std::map<long long, bool>        awaitingName;
inline std::map<long long, std::string> renamingDeviceId;

// ── Подписки пользователей ───────────────────────────────────
inline std::map<long long, std::shared_ptr<ISubscription>> userSubs;

// ── Rate limit ───────────────────────────────────────────────
inline std::map<long long, std::chrono::steady_clock::time_point> lastActionTime;

// ── Пин-коды ─────────────────────────────────────────────────
inline std::map<long long, std::string>                      userPins;
inline std::map<long long, std::pair<std::string, std::string>> awaitingPin;
inline std::set<long long>                                   awaitingSetPin;
inline std::set<long long>                                   awaitingRemovePinConfirm;

// ── Авто-сон ─────────────────────────────────────────────────
inline std::map<long long, std::pair<std::string, std::string>> autoSleepSchedule;
inline std::map<long long, std::string>                         autoSleepFiredDate;
inline std::map<long long, std::string>                         awaitingAutoSleepTime;