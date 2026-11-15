#include <gtest/gtest.h>
#include <cmath>
#include "rebalance/RebalanceBacktest.hpp"
#include "rebalance/RebalanceSettings.hpp"

namespace
{
    // Two months of trading days: January 2026 (2nd to 30th, 20 days) and February (2nd to 27th, 20 days)
    std::vector<std::string> twoMonths()
    {
        std::vector<std::string> days;
        for (int month = 1; month <= 2; month++)
            for (int day = 1, count = 0; count < 20; day++)
            {
                char text[16];
                std::snprintf(text, sizeof(text), "2026-%02d-%02d", month, day);
                days.push_back(text);
                count++;
            }
        return days;
    }

    RebalanceSettings settingsWith(int window = 5)
    {
        RebalanceSettings settings;
        settings.targetStockWeight = 0.6;
        settings.windowDays = window;
        settings.reversalDay = true;
        settings.fullAtDrift = 0.01;
        return settings;
    }

    RebalanceSettings::Variant variant(RebalanceSettings::Legs legs = RebalanceSettings::Legs::Spread,
                                       RebalanceSettings::Sizing sizing = RebalanceSettings::Sizing::Scaled)
    {
        RebalanceSettings::Variant made;
        made.name = "test";
        made.bond = "IEF";
        made.legs = legs;
        made.sizing = sizing;
        made.slippagePercent = 0;
        return made;
    }
}

TEST(RebalanceBacktestTests, MeasuresTheSixtyFortyDriftSinceTheMonthEnd)
{
    RebalanceSettings settings = settingsWith();
    std::vector<std::string> days = twoMonths();
    std::vector<double> stocks(40, 100), bonds(40, 100);
    for (size_t day = 20; day < 40; day++)
        stocks[day] = 110;   // stocks up 10% in February, bonds flat
    RebalanceBacktest backtest(settings, days, stocks, {{"IEF", bonds}});
    std::vector<double> drift = backtest.drift("IEF");
    EXPECT_EQ(drift[5], 0);   // January has no month end before it
    double weight = 0.6 * 1.1 / (0.6 * 1.1 + 0.4);
    EXPECT_NEAR(drift[25], weight - 0.6, 1e-12);
    EXPECT_EQ(backtest.firstOfMonth(25), 20u);
    EXPECT_EQ(backtest.daysToMonthEnd(39), 1);
    EXPECT_EQ(backtest.daysToMonthEnd(35), 5);
}

TEST(RebalanceBacktestTests, ShortsStocksAgainstBondsAheadOfTheMonthEndAndReversesAfter)
{
    RebalanceSettings settings = settingsWith(5);
    std::vector<std::string> days = twoMonths();
    days.push_back("2026-03-02");   // March's first day: the reversal
    std::vector<double> stocks(41, 100), bonds(41, 100);
    for (size_t day = 20; day < 41; day++)
        stocks[day] = 102;   // stocks ahead in February: about +0.5% drift
    RebalanceBacktest backtest(settings, days, stocks, {{"IEF", bonds}});
    std::vector<double> held = backtest.positions(variant());
    double drift = 0.6 * 1.02 / (0.6 * 1.02 + 0.4) - 0.6;
    EXPECT_EQ(held[34], 0);                               // before the window
    EXPECT_NEAR(held[35], -drift / 0.01, 1e-12);          // the last five days: short stocks against bonds
    EXPECT_NEAR(held[39], -drift / 0.01, 1e-12);
    EXPECT_NEAR(held[40], drift / 0.01, 1e-12);           // March 2nd: the other way
    std::vector<double> allIn = backtest.positions(variant(RebalanceSettings::Legs::Spread, RebalanceSettings::Sizing::Sign));
    EXPECT_EQ(allIn[36], -1);
}

TEST(RebalanceBacktestTests, TradesTheSpreadAndTheLongOnlyLeg)
{
    RebalanceSettings settings = settingsWith();
    std::vector<std::string> days = {"2026-01-29", "2026-01-30", "2026-02-02"};
    std::vector<double> stocks = {100, 99, 101}, bonds = {100, 100.5, 100};
    RebalanceBacktest backtest(settings, days, stocks, {{"IEF", bonds}});
    std::vector<double> positions = {0, -1, 0.5};
    auto spread = backtest.trade(variant(), positions, 1, 2);
    EXPECT_NEAR(spread.returns[0], -1 * (-0.01 - 0.005), 1e-12);   // short stocks, long bonds: stocks fell 1%, bonds rose
    EXPECT_NEAR(spread.returns[1], 0.5 * (101.0 / 99 - 1 - (100 / 100.5 - 1)), 1e-12);
    auto longOnly = backtest.trade(variant(RebalanceSettings::Legs::StocksLongOnly), positions, 1, 2);
    EXPECT_EQ(longOnly.returns[0], 0);                              // never short
    EXPECT_NEAR(longOnly.returns[1], 0.5 * (101.0 / 99 - 1), 1e-12);
}

TEST(RebalanceBacktestTests, CoinFlipsAMonthsWindowAndItsReversalTogether)
{
    RebalanceSettings settings = settingsWith(5);
    std::vector<std::string> days = twoMonths();
    days.push_back("2026-03-02");
    RebalanceBacktest backtest(settings, days, std::vector<double>(41, 100), {{"IEF", std::vector<double>(41, 100)}});
    std::vector<double> positions(41, 0);
    for (size_t day = 35; day < 40; day++)
        positions[day] = -0.5;
    positions[40] = 0.5;
    for (unsigned seed = 0; seed < 20; seed++)
    {
        std::mt19937_64 random(seed);
        auto flipped = backtest.coinFlipped(positions, random);
        EXPECT_EQ(flipped[35], flipped[39]);
        EXPECT_EQ(flipped[40], -flipped[39]);   // the reversal stays the other way to its window
        EXPECT_EQ(std::fabs(flipped[35]), 0.5);
    }
}
