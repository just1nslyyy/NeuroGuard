#include "../pch.hpp"
#include <gtest/gtest.h>

#include "../system/DeviceIdentity.hpp"
#include "../hardware/RamMonitor.hpp"
#include "../hardware/DiskMonitor.hpp"
#include "../hardware/NetworkMonitor.hpp"
#include "../system/PowerManager.hpp"

// ── DeviceIdentity ───────────────────────────────────────
TEST(DeviceIdentityTest, HwidNotEmpty) {
    std::string hwid = getHWID();
    EXPECT_FALSE(hwid.empty());
}

TEST(DeviceIdentityTest, HwidContainsDash) {
    std::string hwid = getHWID();
    EXPECT_NE(hwid.find('-'), std::string::npos);
}

// ── RamMonitor ───────────────────────────────────────────
TEST(RamMonitorTest, TotalRamPositive) {
    RamData ram = getRamData();
    EXPECT_GT(ram.totalGb, 0.0f);
}

TEST(RamMonitorTest, UsedRamLessThanTotal) {
    RamData ram = getRamData();
    EXPECT_LE(ram.usedGb, ram.totalGb);
}

TEST(RamMonitorTest, PercentInRange) {
    RamData ram = getRamData();
    EXPECT_GE(ram.percent, 0);
    EXPECT_LE(ram.percent, 100);
}

// ── DiskMonitor ──────────────────────────────────────────
TEST(DiskMonitorTest, DisksNotEmpty) {
    auto disks = getDiskData();
    EXPECT_FALSE(disks.empty());
}

TEST(DiskMonitorTest, EachDiskTotalPositive) {
    auto disks = getDiskData();
    for (auto& d : disks)
        EXPECT_GT(d.totalGb, 0.0f);
}

TEST(DiskMonitorTest, EachDiskPercentInRange) {
    auto disks = getDiskData();
    for (auto& d : disks) {
        EXPECT_GE(d.percent, 0);
        EXPECT_LE(d.percent, 100);
    }
}

TEST(DiskMonitorTest, EachDiskNameNotEmpty) {
    auto disks = getDiskData();
    for (auto& d : disks)
        EXPECT_FALSE(d.name.empty());
}

// ── NetworkMonitor ───────────────────────────────────────
TEST(NetworkMonitorTest, SpeedNonNegative) {
    NetState state;
    NetSpeed speed = getNetSpeed(state);
    EXPECT_GE(speed.downloadMbps, 0.0);
    EXPECT_GE(speed.uploadMbps,   0.0);
}

// ── PowerManager ─────────────────────────────────────────
TEST(PowerManagerTest, SourceNotEmpty) {
    PowerData power = getPowerData();
    EXPECT_FALSE(power.source.empty());
}

TEST(PowerManagerTest, BatteryPercentInRange) {
    PowerData power = getPowerData();
    EXPECT_GE(power.batteryPercent, 0);
    EXPECT_LE(power.batteryPercent, 100);
}

TEST(PowerManagerTest, UptimePositive) {
    PowerData power = getPowerData();
    EXPECT_GT(power.uptime, 0);
}

// ── main ─────────────────────────────────────────────────
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}