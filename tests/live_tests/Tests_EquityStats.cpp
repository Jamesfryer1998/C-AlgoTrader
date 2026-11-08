#include <gtest/gtest.h>
#include <cmath>
#include "../../src/live/EquityStats.hpp"
#include "../../src/util/MarketTime.hpp"

TEST(EquityStatsTests, DrawdownIsTheWorstFallFromAHigh)
{
    EquityStats stats;
    stats.update(100, "2026-10-26 09:30:00");
    stats.update(120, "2026-10-26 10:00:00");   // new high
    stats.update(90, "2026-10-26 11:00:00");    // 25% below it
    stats.update(130, "2026-10-26 12:00:00");   // a higher high...
    stats.update(117, "2026-10-26 13:00:00");   // ...but only 10% below that
    EXPECT_NEAR(stats.maxDrawdown(), 0.25, 1e-12);
}

TEST(EquityStatsTests, CountsDaysFromTheBarTimes)
{
    EquityStats stats;
    EXPECT_EQ(stats.days(), 0);
    stats.update(100, "2026-10-26 09:30:00");
    stats.update(101, "2026-10-26 15:59:00");
    stats.update(102, "2026-10-27 09:30:00");
    EXPECT_EQ(stats.days(), 2);
}

TEST(EquityStatsTests, SharpeOfDailyReturns)
{
    EquityStats stats;
    stats.update(100, "2026-10-26 09:30:00");   // starts at 100
    stats.update(101, "2026-10-26 15:59:00");   // day 1 closes +1%
    stats.update(101, "2026-10-27 15:59:00");   // day 2 flat
    stats.update(102.01, "2026-10-28 15:59:00");   // day 3 +1%

    // Returns 1%, 0%, 1%: mean 2/3%, sample standard deviation 0.577%
    double mean = 0.02 / 3, deviation = std::sqrt((2 * std::pow(0.01 - mean, 2) + std::pow(mean, 2)) / 2);
    double expected = mean / deviation * std::sqrt(static_cast<double>(MarketTime::TRADING_DAYS_PER_YEAR));
    EXPECT_NEAR(stats.sharpe(), expected, 1e-9);
}

TEST(EquityStatsTests, NoSharpeWithUnderTwoDaysOrNoVariation)
{
    EquityStats oneDay;
    oneDay.update(100, "2026-10-26 09:30:00");
    oneDay.update(105, "2026-10-26 15:59:00");
    EXPECT_DOUBLE_EQ(oneDay.sharpe(), 0);

    EquityStats flat;
    flat.update(100, "2026-10-26 15:59:00");
    flat.update(100, "2026-10-27 15:59:00");
    EXPECT_DOUBLE_EQ(flat.sharpe(), 0);
}

TEST(EquityStatsTests, IgnoresMissingAccountValues)
{
    // An account that couldn't be read reports 0: that's not a 100% drawdown
    EquityStats stats;
    stats.update(100, "2026-10-26 09:30:00");
    stats.update(0, "2026-10-26 09:31:00");
    EXPECT_DOUBLE_EQ(stats.maxDrawdown(), 0);
}
