#include <gtest/gtest.h>
#include <string>
#include "../../src/backtest/StrategyTester.hpp"
#include "../../src/util/Log.hpp"

namespace
{
    const int MINUTES_PER_DAY = 390;
    const size_t LATE_AFTERNOON_FIRST = 350;   // the 15:20 bar
    const size_t LATE_AFTERNOON_END = 384;     // up to the 15:53 bar
    const size_t HISTORY_ONLY = 24;            // 15:20 to 15:43 only warm the strategy up: it first decides at 15:44

    std::string twoDigits(int number)
    {
        return (number < 10 ? "0" : "") + std::to_string(number);
    }

    // Two sessions of a steady slide: RSI stays oversold, so the strategy wants to be long all the time
    std::vector<MarketCondition> slidingDays()
    {
        std::vector<MarketCondition> bars;
        double price = 100;
        for (int d = 0; d < 2; d++)
            for (int m = 0; m < MINUTES_PER_DAY; m++)
            {
                price -= 0.01;
                std::string time = "2026-03-0" + std::to_string(2 + d) + " " + twoDigits(9 + (30 + m) / 60) + ":"
                                 + twoDigits((30 + m) % 60) + ":00";
                bars.emplace_back(time, "TEST", static_cast<float>(price), static_cast<float>(price), 1000, "1m");
            }
        return bars;
    }

    json alwaysBuyingConfig(bool flatAtClose)
    {
        return {{"ticker", "TEST"}, {"max_position_size", 10}, {"max_exposure", 5.0}, {"slippage_tolerance", 2.0},
                {"bars_per_year", 252.0 * MINUTES_PER_DAY},
                {"end_of_day", {{"flat_at_close", flatAtClose}}},
                {"strategies", {{{"name", "RSI"}, {"active", 1}, {"period", 14}, {"oversold_threshold", 99.0},
                                 {"overbought_threshold", 100.0}, {"stop_loss", 50}, {"take_profit", 50}}}}};
    }

    float heldAfter(const std::vector<MarketCondition>& bars, size_t count, bool flatAtClose, size_t warmup = 0)
    {
        StrategyTester tester(alwaysBuyingConfig(flatAtClose));
        tester.keepEquityCurve(false);
        tester.setWarmupBars(warmup);
        tester.runOn(std::vector<MarketCondition>(bars.begin(), bars.begin() + count));
        return tester.getBroker().getLatestPosition("TEST").getQuantity();
    }
}

TEST(DayEndBacktestTests, NothingIsHeldOvernightAndNothingOpensNearTheClose)
{
    Log::verbose = false;
    std::vector<MarketCondition> bars = slidingDays();

    EXPECT_GT(heldAfter(bars, MINUTES_PER_DAY, false), 0) << "without the rule it holds overnight";
    EXPECT_FLOAT_EQ(heldAfter(bars, MINUTES_PER_DAY, true), 0);
    EXPECT_GT(heldAfter(bars, MINUTES_PER_DAY + 60, true), 0) << "and buys again the next morning";

    // Flat and wanting in from 15:44 to 15:53: too close to the close to open anything
    std::vector<MarketCondition> lateAfternoon(bars.begin() + LATE_AFTERNOON_FIRST, bars.begin() + LATE_AFTERNOON_END);
    EXPECT_GT(heldAfter(lateAfternoon, lateAfternoon.size(), false, HISTORY_ONLY), 0);
    EXPECT_FLOAT_EQ(heldAfter(lateAfternoon, lateAfternoon.size(), true, HISTORY_ONLY), 0);
    Log::verbose = true;
}
