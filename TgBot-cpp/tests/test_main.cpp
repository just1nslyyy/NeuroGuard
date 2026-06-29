// ═══════════════════════════════════════════════════════════
//  test_main.cpp
//  Google Test — тесты для NeuroGuard Bot
// ═══════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "../subscription/SubFactory.hpp"
#include "../utils/BotHelpers.hpp"

using json = nlohmann::json;

// ── SubFactory ───────────────────────────────────────────────
TEST(SubFactoryTest, CreateFree) {
    auto sub = SubFactory::create("FREE");
    EXPECT_NE(sub, nullptr);
    EXPECT_EQ(sub->getRankName(), "FREE");
}

TEST(SubFactoryTest, CreateSentinel) {
    auto sub = SubFactory::create("SENTINEL");
    EXPECT_NE(sub, nullptr);
    EXPECT_EQ(sub->getRankName(), "SENTINEL");
}

TEST(SubFactoryTest, CreateOverseer) {
    auto sub = SubFactory::create("OVERSEER");
    EXPECT_NE(sub, nullptr);
    EXPECT_EQ(sub->getRankName(), "OVERSEER");
}

TEST(SubFactoryTest, CreateUnknownDefaultsFree) {
    auto sub = SubFactory::create("UNKNOWN");
    EXPECT_NE(sub, nullptr);
    EXPECT_EQ(sub->getRankName(), "FREE");
}

TEST(SubFactoryTest, CreateLowercase) {
    auto sub = SubFactory::create("overseer");
    EXPECT_NE(sub, nullptr);
    EXPECT_EQ(sub->getRankName(), "OVERSEER");
}

// ── Permissions ──────────────────────────────────────────────
TEST(PermissionsTest, FreeCannotDoAnything) {
    auto sub = SubFactory::create("FREE");
    EXPECT_FALSE(sub->canControlPower());
    EXPECT_FALSE(sub->canTakeScreenshots());
    EXPECT_FALSE(sub->canKillProcesses());
}

TEST(PermissionsTest, SentinelCanControlPower) {
    auto sub = SubFactory::create("SENTINEL");
    EXPECT_TRUE(sub->canControlPower());
    EXPECT_FALSE(sub->canTakeScreenshots());
    EXPECT_FALSE(sub->canKillProcesses());
}

TEST(PermissionsTest, OverseerCanDoEverything) {
    auto sub = SubFactory::create("OVERSEER");
    EXPECT_TRUE(sub->canControlPower());
    EXPECT_TRUE(sub->canTakeScreenshots());
    EXPECT_TRUE(sub->canKillProcesses());
}

// ── formatStatus ─────────────────────────────────────────────
inline json makeDevice(bool online = true) {
    json dev;
    dev["name"]           = "TestPC";
    dev["online"]         = online;
    dev["cpu"]            = 45.0f;
    dev["gpu"]            = 30.0f;
    dev["ram"]            = 60.0f;
    dev["cpu_temp"]       = 55.0f;
    dev["gpu_temp"]       = 50.0f;
    dev["ram_used"]       = 8.0f;
    dev["ram_total"]      = 16.0f;
    dev["uptime"]         = 3600;
    dev["power_source"]   = "AC";
    dev["last_seen_time"] = "12:00:00";
    dev["cpu_name"]       = "Intel i7";
    dev["gpu_name"]       = "RTX 3080";
    dev["net_down"]       = 1.5;
    dev["net_up"]         = 0.5;
    dev["disk_name"]      = "Samsung SSD";
    dev["hwid"]           = "ABC123";
    return dev;
}

TEST(FormatStatusTest, FreeOnlineNotEmpty) {
    auto sub = SubFactory::create("FREE");
    auto dev = makeDevice(true);
    std::string result = sub->formatStatus(dev);
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find("FREE"), std::string::npos);
    EXPECT_NE(result.find("TestPC"), std::string::npos);
}

TEST(FormatStatusTest, FreeOfflineNotEmpty) {
    auto sub = SubFactory::create("FREE");
    auto dev = makeDevice(false);
    std::string result = sub->formatStatus(dev);
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find("офлайн"), std::string::npos);
}

TEST(FormatStatusTest, SentinelOnlineNotEmpty) {
    auto sub = SubFactory::create("SENTINEL");
    auto dev = makeDevice(true);
    std::string result = sub->formatStatus(dev);
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find("SENTINEL"), std::string::npos);
}

TEST(FormatStatusTest, OverseerOnlineNotEmpty) {
    auto sub = SubFactory::create("OVERSEER");
    auto dev = makeDevice(true);
    std::string result = sub->formatStatus(dev);
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find("OVERSEER"), std::string::npos);
}

// ── parseTime ────────────────────────────────────────────────
TEST(ParseTimeTest, ValidTime) {
    auto [h, m] = parseTime("23:30");
    EXPECT_EQ(h, 23);
    EXPECT_EQ(m, 30);
}

TEST(ParseTimeTest, MidnightTime) {
    auto [h, m] = parseTime("00:00");
    EXPECT_EQ(h, 0);
    EXPECT_EQ(m, 0);
}

TEST(ParseTimeTest, InvalidFormat) {
    auto [h, m] = parseTime("2330");
    EXPECT_EQ(h, -1);
    EXPECT_EQ(m, -1);
}

TEST(ParseTimeTest, InvalidHour) {
    auto [h, m] = parseTime("25:00");
    EXPECT_EQ(h, -1);
}

TEST(ParseTimeTest, InvalidMinute) {
    auto [h, m] = parseTime("12:60");
    EXPECT_EQ(h, -1);
}

// ── safeStoi ─────────────────────────────────────────────────
TEST(SafeStoiTest, ValidNumber) {
    int result = 0;
    EXPECT_TRUE(safeStoi("42", result));
    EXPECT_EQ(result, 42);
}

TEST(SafeStoiTest, ZeroNumber) {
    int result = 0;
    EXPECT_TRUE(safeStoi("0", result));
    EXPECT_EQ(result, 0);
}

TEST(SafeStoiTest, InvalidString) {
    int result = 0;
    EXPECT_FALSE(safeStoi("abc", result));
}

TEST(SafeStoiTest, Overflow) {
    int result = 0;
    EXPECT_FALSE(safeStoi("99999999999999", result));
}

// ── base64Encode ─────────────────────────────────────────────
TEST(Base64Test, SimpleString) {
    std::string result = base64Encode("hello");
    EXPECT_EQ(result, "aGVsbG8=");
}

TEST(Base64Test, EmptyString) {
    std::string result = base64Encode("");
    EXPECT_TRUE(result.empty());
}

TEST(Base64Test, CredentialsFormat) {
    std::string result = base64Encode("shop_id:secret");
    EXPECT_FALSE(result.empty());
    EXPECT_EQ(result.size() % 4, 0);
}

// ── main ─────────────────────────────────────────────────────
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}