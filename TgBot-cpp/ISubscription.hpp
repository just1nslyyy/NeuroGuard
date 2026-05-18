#pragma once
#include <string>
#include <memory>
#include <tgbot/tgbot.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class ISubscription {
public:
    virtual ~ISubscription() = default;

    virtual std::string getRankName() = 0;

    // ✅ Убираем чисто виртуальный метод, даём пустую реализацию
    // Дочерние классы могут переопределить, но не обязаны
    virtual void displayProfile(TgBot::Bot &bot, TgBot::User::Ptr user,
                                const json &devices,
                                const std::string &photoPath = "") {
    }

    virtual std::string formatStatus(const json &dev) = 0;

    virtual bool canControlPower()    { return false; }
    virtual bool canTakeScreenshots() { return false; }
    virtual bool canKillProcesses()   { return false; }
};