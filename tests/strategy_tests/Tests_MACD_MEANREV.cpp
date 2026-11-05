#include <gtest/gtest.h>
#include <cmath>
#include "../../src/strategy_engine/MACD.hpp"
#include "../../src/strategy_engine/MEANREV.hpp"

namespace
{
    // Feed closes one at a time like the engine does; return the signal raised on each bar ("" = none)
    template <typename Strategy>
    std::vector<std::string> run(Strategy& strategy, const std::vector<float>& closes)
    {
        MarketData data;
        std::vector<std::string> signals;
        for (size_t i = 0; i < closes.size(); i++)
        {
            data.addBar(MarketCondition("t" + std::to_string(i), "NVDA", closes[i], closes[i], 100, "1m"));
            std::optional<Order> order = strategy.onBar(data, 0);
            signals.push_back(order ? order->getTypeAsString() : "");
        }
        return signals;
    }

    StrategyAttribute macdAttributes(const std::string& type = "crossover")
    {
        return StrategyAttribute(json{{"name", "MACD"}, {"short_period", 3}, {"long_period", 6},
                                      {"signal_period", 3}, {"signal_type", type}, {"stop_loss", 5}, {"take_profit", 5}});
    }

    StrategyAttribute meanrevAttributes()
    {
        return StrategyAttribute(json{{"name", "MEANREV"}, {"lookback_period", 10}, {"moving_average_period", 10},
                                      {"entry_threshold", 1.5}, {"exit_threshold", 0.25}, {"stop_loss", 5}, {"take_profit", 5}});
    }

    int count(const std::vector<std::string>& signals, const std::string& type)
    {
        return static_cast<int>(std::count(signals.begin(), signals.end(), type));
    }
}

// ---------- MACD ----------

TEST(MACDTests, FlatPriceHasZeroMacd)
{
    MACD::Values v = MACD::calculate(std::vector<float>(100, 50.0f), 12, 26, 9);
    EXPECT_NEAR(v.macd, 0.0, 1e-9);
    EXPECT_NEAR(v.signal, 0.0, 1e-9);
}

TEST(MACDTests, RisingPriceHasPositiveMacd)
{
    std::vector<float> closes;
    for (int i = 0; i < 100; i++) closes.push_back(100.0f + i);
    MACD::Values v = MACD::calculate(closes, 12, 26, 9);
    EXPECT_GT(v.macd, 0.0);   // short EMA above long EMA in an uptrend
}

TEST(MACDTests, CrossoverBuysOnceWhenFallTurnsToRise)
{
    std::vector<float> closes;
    for (int i = 0; i < 60; i++) closes.push_back(200.0f - i);   // falling
    for (int i = 0; i < 60; i++) closes.push_back(140.0f + i);   // rising

    MACD macd(macdAttributes());
    std::vector<std::string> signals = run(macd, closes);

    EXPECT_EQ(count(signals, "BUY"), 1);
    EXPECT_EQ(count(signals, "SELL"), 0);
    int buyBar = static_cast<int>(std::find(signals.begin(), signals.end(), "BUY") - signals.begin());
    EXPECT_GE(buyBar, 60);   // only after the turn
    EXPECT_LT(buyBar, 70);   // and soon after it
}

TEST(MACDTests, CrossoverSellsWhenRiseTurnsToFall)
{
    std::vector<float> closes;
    for (int i = 0; i < 60; i++) closes.push_back(100.0f + i);
    for (int i = 0; i < 60; i++) closes.push_back(160.0f - i);

    MACD macd(macdAttributes());
    std::vector<std::string> signals = run(macd, closes);
    EXPECT_EQ(count(signals, "SELL"), 1);
    EXPECT_EQ(count(signals, "BUY"), 0);
}

TEST(MACDTests, ZeroCrossWaitsForMacdToChangeSign)
{
    std::vector<float> closes;
    for (int i = 0; i < 60; i++) closes.push_back(200.0f - i);
    for (int i = 0; i < 60; i++) closes.push_back(140.0f + i);

    MACD crossover(macdAttributes("crossover"));
    MACD zero(macdAttributes("zero_cross"));
    std::vector<std::string> a = run(crossover, closes);
    std::vector<std::string> b = run(zero, closes);

    int crossoverBar = static_cast<int>(std::find(a.begin(), a.end(), "BUY") - a.begin());
    int zeroBar = static_cast<int>(std::find(b.begin(), b.end(), "BUY") - b.begin());
    ASSERT_LT(zeroBar, static_cast<int>(closes.size()));
    EXPECT_GT(zeroBar, crossoverBar);   // the zero line is crossed later than the signal line
}

TEST(MACDTests, ValidationRejectsBadSettings)
{
    StrategyAttribute bad = macdAttributes();
    bad.short_period = 30;   // not shorter than long
    EXPECT_THROW(MACD(bad).validate(), std::runtime_error);

    StrategyAttribute badType = macdAttributes("sideways");
    EXPECT_THROW(MACD(badType).validate(), std::runtime_error);

    EXPECT_NO_THROW(MACD(macdAttributes()).validate());
}

// ---------- MEANREV ----------

TEST(MEANREVTests, ZScore)
{
    // mean 10, population std dev of {8, 12, 8, 12} = 2, last close 12 -> z = +1
    std::vector<float> closes = {8, 12, 8, 12};
    EXPECT_NEAR(MEANREV::zScore(closes, 4, 4), 1.0, 1e-9);
    EXPECT_NEAR(MEANREV::zScore(std::vector<float>(10, 5.0f), 10, 10), 0.0, 1e-9);   // flat: no signal
}

TEST(MEANREVTests, BuysTheDipThenSellsWhenPriceReturns)
{
    std::vector<float> closes;
    for (int i = 0; i < 30; i++) closes.push_back(100.0f + (i % 2 ? 0.5f : -0.5f));   // quiet around 100
    closes.push_back(96.0f);                                                          // sharp dip
    for (int i = 0; i < 20; i++) closes.push_back(100.0f + (i % 2 ? 0.5f : -0.5f));   // back to normal

    MEANREV meanrev(meanrevAttributes());
    std::vector<std::string> signals = run(meanrev, closes);

    EXPECT_EQ(signals[30], "BUY");           // entered on the dip
    EXPECT_EQ(count(signals, "BUY"), 1);
    EXPECT_EQ(count(signals, "SELL"), 1);    // closed once the price came back
    EXPECT_EQ(meanrev.getState(), 0);
}

TEST(MEANREVTests, SellsTheSpikeThenBuysBack)
{
    std::vector<float> closes;
    for (int i = 0; i < 30; i++) closes.push_back(100.0f + (i % 2 ? 0.5f : -0.5f));
    closes.push_back(104.0f);                                                         // spike
    for (int i = 0; i < 20; i++) closes.push_back(100.0f + (i % 2 ? 0.5f : -0.5f));

    MEANREV meanrev(meanrevAttributes());
    std::vector<std::string> signals = run(meanrev, closes);
    EXPECT_EQ(signals[30], "SELL");
    EXPECT_EQ(count(signals, "SELL"), 1);
    EXPECT_EQ(count(signals, "BUY"), 1);
}

TEST(MEANREVTests, NoTradesInAQuietMarket)
{
    std::vector<float> closes;
    for (int i = 0; i < 100; i++) closes.push_back(100.0f + (i % 2 ? 0.5f : -0.5f));
    MEANREV meanrev(meanrevAttributes());
    std::vector<std::string> signals = run(meanrev, closes);
    EXPECT_EQ(count(signals, "BUY") + count(signals, "SELL"), 0);
}

TEST(MEANREVTests, ValidationRejectsBadSettings)
{
    StrategyAttribute bad = meanrevAttributes();
    bad.exit_threshold = 2.0;   // not below entry
    EXPECT_THROW(MEANREV(bad).validate(), std::runtime_error);
    EXPECT_NO_THROW(MEANREV(meanrevAttributes()).validate());
}

// ---------- status lines (shown in the live decisions output) ----------

TEST(StrategyStatusTests, EmptyBeforeEnoughBars)
{
    MEANREV meanrev(meanrevAttributes());
    MACD macd(macdAttributes());
    EXPECT_EQ(meanrev.status(), "");
    EXPECT_EQ(macd.status(), "");
}

TEST(StrategyStatusTests, MeanRevShowsEntryLevelsWhenFlatAndExitLevelWhenIn)
{
    std::vector<float> closes;
    for (int i = 0; i < 30; i++) closes.push_back(100.0f + (i % 2 ? 0.5f : -0.5f));   // mean 100, sd 0.5
    MEANREV meanrev(meanrevAttributes());
    run(meanrev, closes);
    // entry 1.5 sd either side of 100
    EXPECT_EQ(meanrev.status(), "MEANREV(10/10)   L: 99.25 | C: 100.50 | U: 100.75   (z +1.00, BUY below L, SELL above U)");

    closes.push_back(96.0f);   // dip -> long
    MEANREV dipped(meanrevAttributes());
    run(dipped, closes);
    ASSERT_EQ(dipped.getState(), 1);
    EXPECT_NE(dipped.status().find("long, C: 96.00 | exit at >= "), std::string::npos) << dipped.status();
}

TEST(StrategyStatusTests, MacdShowsGapToSignalLine)
{
    std::vector<float> closes;
    for (int i = 0; i < 60; i++) closes.push_back(100.0f + i);   // rising: MACD above its signal line
    MACD macd(macdAttributes());
    run(macd, closes);
    std::string line = macd.status();
    EXPECT_EQ(line.rfind("MACD(3/6/3)   MACD ", 0), 0u) << line;
    EXPECT_NE(line.find("| signal "), std::string::npos) << line;
    EXPECT_NE(line.find("above: SELL if it crosses below the signal line"), std::string::npos) << line;

    MACD zero(macdAttributes("zero_cross"));
    run(zero, closes);
    EXPECT_NE(zero.status().find("| zero line   above: SELL if it crosses below 0"), std::string::npos) << zero.status();
}

TEST(StrategyStatusTests, BarsNeededMatchesTheSettings)
{
    EXPECT_EQ(MEANREV(meanrevAttributes()).barsNeeded(), 10);
    EXPECT_EQ(MACD(macdAttributes()).barsNeeded(), 5 * (6 + 3));
    StrategyAttribute longLookback(json{{"name", "MEANREV"}, {"lookback_period", 120}, {"moving_average_period", 20},
                                        {"entry_threshold", 2.0}, {"exit_threshold", 0.0}});
    EXPECT_EQ(MEANREV(longLookback).barsNeeded(), 120);
}
