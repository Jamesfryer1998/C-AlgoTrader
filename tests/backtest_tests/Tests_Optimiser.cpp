#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include "../../src/backtest/Optimiser.hpp"
#include "../../src/util/Log.hpp"

namespace
{
    std::vector<MarketCondition> market(int days, int barsPerDay = 60)
    {
        std::vector<MarketCondition> bars;
        unsigned int seed = 99;
        double price = 100;
        for (int d = 0; d < days; d++)
            for (int m = 0; m < barsPerDay; m++)
            {
                seed = seed * 1103515245u + 12345u;
                double previous = price;
                price *= 1.0 + (((seed >> 16) % 1000) / 1000.0 - 0.5) * 0.004 + 0.0004 * std::sin((d * barsPerDay + m) / 30.0);
                char time[32];
                int month = 1 + (d / 20) % 12, year = 2024 + d / 240, day = 1 + d % 20;
                std::snprintf(time, sizeof(time), "%04d-%02d-%02d %02d:%02d:00", year, month, day, 10 + m / 60, m % 60);
                bars.emplace_back(time, "NVDA", static_cast<float>(previous), static_cast<float>(price), 1000, "1m");
            }
        return bars;
    }

    json baseConfig()
    {
        return {{"ticker", "NVDA"}, {"max_position_size", 10}, {"max_exposure", 5.0}, {"slippage_tolerance", 2.0},
                {"strategies", {{{"name", "RSI"}, {"active", 1}, {"period", 30}, {"overbought_threshold", 75.0},
                                 {"oversold_threshold", 25.0}, {"stop_loss", 10}, {"take_profit", 10}},
                                {{"name", "MACD"}, {"active", 0}, {"short_period", 12}, {"long_period", 26}, {"signal_period", 9}}}}};
    }

    json optimisationSettings()
    {
        return {{"starting_capital", 100000.0}, {"commission_per_trade", 0.0}, {"slippage_percent", 0.01},
                {"objective", "sharpe"}, {"min_closed_trades", 1}, {"test_months", 2}, {"top_n_to_test", 3},
                {"max_combinations", 0},
                {"strategies", {
                    {"RSI", {{"period", {7, 14}}, {"oversold_threshold", {25, 30}}, {"overbought_threshold", {70, 75}}}},
                    {"MACD", {{"short_period", {12, 30}}, {"long_period", {26}}, {"signal_period", {9}}}}
                }}};
    }
}

TEST(OptimiserTests, GridIsEveryCombinationOnTopOfCurrentSettings)
{
    Optimiser optimiser(baseConfig(), optimisationSettings());
    std::vector<json> grid = optimiser.parameterGrid("RSI");
    ASSERT_EQ(grid.size(), 8u);   // 2 x 2 x 2
    for (const auto& s : grid)
    {
        EXPECT_EQ(s["active"], 1);
        EXPECT_EQ(s["stop_loss"], 10);   // not searched: kept from strategy.json
    }
}

TEST(OptimiserTests, InvalidCombinationsAreDropped)
{
    Optimiser optimiser(baseConfig(), optimisationSettings());
    std::vector<json> grid = optimiser.parameterGrid("MACD");
    ASSERT_EQ(grid.size(), 1u);   // short 30 >= long 26 is impossible
    EXPECT_EQ(grid[0]["short_period"], 12);
}

TEST(OptimiserTests, SamplingIsRepeatable)
{
    json settings = optimisationSettings();
    settings["max_combinations"] = 3;
    Optimiser optimiser(baseConfig(), settings);
    std::vector<json> a = optimiser.parameterGrid("RSI");
    std::vector<json> b = optimiser.parameterGrid("RSI");
    ASSERT_EQ(a.size(), 3u);
    EXPECT_EQ(a, b);
}

TEST(OptimiserTests, SplitKeepsTheLastMonthsForTesting)
{
    std::vector<MarketCondition> bars;
    for (int month = 1; month <= 12; month++)
    {
        char time[32];
        std::snprintf(time, sizeof(time), "2025-%02d-15 10:00:00", month);
        bars.emplace_back(time, "NVDA", 1, 1, 1, "1m");
    }
    std::vector<MarketCondition> train, test;
    Optimiser::splitByMonths(bars, 3, train, test);
    ASSERT_EQ(train.size(), 9u);
    ASSERT_EQ(test.size(), 3u);
    EXPECT_EQ(test.front().DateTime.substr(0, 7), "2025-10");
    EXPECT_EQ(train.back().DateTime.substr(0, 7), "2025-09");
}

TEST(OptimiserTests, SameResultsWithOneThreadOrMany)
{
    Log::verbose = false;
    std::vector<MarketCondition> bars = market(60);
    std::vector<MarketCondition> train, test;
    Optimiser::splitByMonths(bars, 1, train, test);

    Optimiser optimiser(baseConfig(), optimisationSettings());
    std::vector<json> grid = optimiser.parameterGrid("RSI");
    auto serial = optimiser.search(grid, train, test, 1);
    auto parallel = optimiser.search(grid, train, test, 4);
    Log::verbose = true;

    ASSERT_EQ(serial.size(), parallel.size());
    for (size_t i = 0; i < serial.size(); i++)
    {
        EXPECT_EQ(serial[i].settings, parallel[i].settings);
        EXPECT_DOUBLE_EQ(serial[i].train.sharpeRatio, parallel[i].train.sharpeRatio);
        EXPECT_EQ(serial[i].train.fills, parallel[i].train.fills);
        EXPECT_EQ(serial[i].tested, parallel[i].tested);
    }
}

TEST(OptimiserTests, ResultsAreRankedAndOnlyTheTopAreTested)
{
    Log::verbose = false;
    std::vector<MarketCondition> bars = market(60);
    std::vector<MarketCondition> train, test;
    Optimiser::splitByMonths(bars, 1, train, test);
    Optimiser optimiser(baseConfig(), optimisationSettings());
    auto results = optimiser.search(optimiser.parameterGrid("RSI"), train, test, 2);
    Log::verbose = true;

    ASSERT_FALSE(test.empty());
    int tested = 0;
    for (size_t i = 0; i < results.size(); i++)
    {
        if (results[i].tested)
            tested++;
        if (i > 0 && results[i].eligible && results[i - 1].eligible)
        {
            EXPECT_GE(results[i - 1].score, results[i].score);   // best first
        }
    }
    EXPECT_LE(tested, 3);   // top_n_to_test
    EXPECT_TRUE(results[0].tested || !results[0].eligible);
}
