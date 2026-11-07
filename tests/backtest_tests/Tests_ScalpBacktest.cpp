#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include "../../src/backtest/StrategyTester.hpp"
#include "../../src/util/Log.hpp"

namespace
{
    // `days` sessions of regular-hours minute bars that wobble around a slowly drifting level, with the
    // occasional sharp stretch that snaps back: what a VWAP fade looks for
    std::vector<MarketCondition> scalpDays(int days)
    {
        std::vector<MarketCondition> bars;
        unsigned seed = 11;
        double level = 100;
        for (int d = 0; d < days; d++)
        {
            char day[16];
            std::snprintf(day, sizeof(day), "2026-03-%02d", 2 + d);
            double stretch = 0;
            for (int m = 0; m < 390; m++)
            {
                seed = seed * 1103515245u + 12345u;
                double noise = (((seed >> 16) % 1000) / 1000.0 - 0.5) * 0.06;
                if (m % 47 == 20) stretch = (m % 2 ? 1.0 : -1.0) * 0.6;   // a sudden stretch...
                stretch *= 0.8;                                          // ...that fades quickly
                level += 0.001;
                double price = level + noise + stretch;
                char time[32];
                std::snprintf(time, sizeof(time), "%s %02d:%02d:00", day, 9 + (30 + m) / 60, (30 + m) % 60);
                bars.emplace_back(time, "TEST", static_cast<float>(price), static_cast<float>(price), 1000, "1m");
            }
        }
        return bars;
    }

    json scalpConfig()
    {
        return {{"ticker", "TEST"}, {"max_position_size", 10}, {"max_exposure", 5.0}, {"slippage_tolerance", 2.0},
                {"bars_per_year", 252.0 * 390.0},
                {"strategies", {{{"name", "SCALP"}, {"active", 1}, {"entry_threshold", 2.0}, {"exit_threshold", 0.0},
                                 {"max_hold_bars", 10}, {"warmup_minutes", 15}, {"stop_loss", 0.5}, {"take_profit", 1.0}}}}};
    }
}

TEST(ScalpBacktestTests, ScalpsAndIsFlatAtTheEndOfEveryDay)
{
    Log::verbose = false;
    std::vector<MarketCondition> bars = scalpDays(3);

    // Run each day's history up to the last bar of that day: the position must be flat every evening
    for (int days = 1; days <= 3; days++)
    {
        std::vector<MarketCondition> upTo(bars.begin(), bars.begin() + days * 390);
        StrategyTester tester(scalpConfig());
        tester.setCommissionPerTrade(0);
        tester.keepEquityCurve(false);
        tester.runOn(upTo);

        EXPECT_FLOAT_EQ(tester.getBroker().getLatestPosition("TEST").getQuantity(), 0) << "day " << days;
        const auto& fills = tester.getBroker().getFilledOrders();
        EXPECT_GE(fills.size(), static_cast<size_t>(4 * days)) << "day " << days;   // it does trade
        EXPECT_EQ(fills.size() % 2, 0u) << "every trade opened was closed";
    }
    Log::verbose = true;
}
