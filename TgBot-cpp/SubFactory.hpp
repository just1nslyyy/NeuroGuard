#pragma once
#include <memory>
#include <string>
#include <algorithm>
#include "SubscriptionModel.hpp"

class SubFactory {
public:
    static std::shared_ptr<ISubscription> create(const std::string& rank) {
        std::string r = rank;
        std::transform(r.begin(), r.end(), r.begin(), ::toupper);

        if (r == "OVERSEER") return std::make_shared<OverseerSub>();
        if (r == "SENTINEL") return std::make_shared<SentinelSub>();
        return std::make_shared<FreeSub>();
    }
};