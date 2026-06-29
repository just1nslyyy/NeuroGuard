#pragma once
// ═══════════════════════════════════════════════════════════
//  AuthManager.hpp
//  Авторизация устройства через Telegram бот
// ═══════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <string>
#include <iostream>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

inline void requestAuthCode(const std::string& hwid,
                             const std::string& serverUrl) {
    try {
        auto r = cpr::Post(
            cpr::Url{serverUrl + "/auth/generate/" + hwid},
            cpr::Timeout{8000});

        if (r.status_code == 200) {
            auto d = json::parse(r.text);
            if (d.contains("code")) {
                std::string code = d["code"].get<std::string>();
                std::string cmd  = "/auth " + code;

                std::cout << "\n";
                std::cout << "  +===========================================+\n";
                std::cout << "  |         NEUROGUARD - ПРИВЯЗКА ПК          |\n";
                std::cout << "  +===========================================+\n";
                std::cout << "  |                                           |\n";
                std::cout << "  |  ШАГ 1: Откройте Telegram                 |\n";
                std::cout << "  |  ШАГ 2: Найдите @NeuroGuardProBot         |\n";
                std::cout << "  |  ШАГ 3: Отправьте боту:                   |\n";
                std::cout << "  |                                           |\n";
                std::cout << "  |   " << cmd;
                int sp = 43 - (int)cmd.size() - 3;
                if (sp < 1) sp = 1;
                std::cout << std::string(sp, ' ') << "|\n";
                std::cout << "  |                                           |\n";
                std::cout << "  +===========================================+\n";
                std::cout << "  >> Ваш код: " << code << "\n\n";
                std::cout << "  Это окно можно закрыть после привязки.\n\n";

            } else if (d.value("status", "") == "already_linked") {
                std::cout << "\n  [*] Устройство уже привязано. Агент работает.\n\n";
            }
        } else {
            std::cout << "\n  [!] Сервер недоступен (код: "
                      << r.status_code << ")\n\n";
        }
    } catch (...) {
        std::cout << "\n  [!] Ошибка подключения к серверу.\n\n";
    }
}