#include <gtest/gtest.h>
#include <cstdio>
#include "../../src/strategy_engine/MACD.hpp"
#include "../../src/strategy_engine/MEANREV.hpp"
#include "../../src/strategy_engine/RSI.hpp"
#include "../../src/strategy_engine/SCALP.hpp"
#include "../../src/strategy_engine/StrategyEngine.hpp"
#include "../../src/strategy_engine/StrategyFactory.hpp"

// The columns each strategy gives the live app's per-minute table
namespace
{
    MarketData bars(const std::vector<float>& closes)
    {
        MarketData data;
        int minute = 10 * 60;
        for (float c : closes)
        {
            char time[32];
            std::snprintf(time, sizeof(time), "2026-11-05 %02d:%02d:00", minute / 60, minute % 60);
            data.addBar(MarketCondition(time, "TEST", c, c, 1000, "1m"), 1000);
            minute++;
        }
        return data;
    }

    template <typename S>
    void run(S& strategy, const MarketData& data)
    {
        strategy.onBar(data, 0);
    }
}

TEST(LevelsTests, RsiThresholdsAroundTheRsi)
{
    RSI rsi(StrategyAttribute(json{{"name", "RSI"}, {"period", 5}, {"oversold_threshold", 30}, {"overbought_threshold", 70}}));
    EXPECT_FALSE(rsi.decisionLevels().ready);
    EXPECT_EQ(rsi.decisionLevels().label, "RSI(5)");
    MarketData data = bars({100, 101, 102, 103, 104, 105});   // only rises: RSI 100
    run(rsi, data);
    auto l = rsi.decisionLevels();
    EXPECT_TRUE(l.ready);
    EXPECT_EQ(l.lower, "30.00");
    EXPECT_EQ(l.current, "100.00");
    EXPECT_EQ(l.upper, "70.00");
    EXPECT_EQ(l.state, "in SELL zone");
}

TEST(LevelsTests, MeanrevBandsWhenFlatAndExitWhenIn)
{
    MEANREV meanrev(StrategyAttribute(json{{"name", "MEANREV"}, {"lookback_period", 10}, {"moving_average_period", 10},
                                           {"entry_threshold", 1.5}, {"exit_threshold", 0.0}}));
    std::vector<float> closes;
    for (int i = 0; i < 20; i++) closes.push_back(i % 2 ? 100.5f : 99.5f);   // mean 100, sd 0.5
    MarketData quiet = bars(closes);
    run(meanrev, quiet);
    auto flat = meanrev.decisionLevels();
    EXPECT_EQ(flat.label, "MEANREV(10/10)");
    EXPECT_EQ(flat.lower, "99.25");
    EXPECT_EQ(flat.upper, "100.75");
    EXPECT_EQ(flat.current, "100.50");
    EXPECT_EQ(flat.state.rfind("flat, z +", 0), 0u) << flat.state;

    closes.push_back(97.0f);   // dip: goes long
    MarketData dip = bars(closes);
    run(meanrev, dip);
    auto in = meanrev.decisionLevels();
    EXPECT_EQ(in.lower, "");
    EXPECT_EQ(in.upper.rfind("exit ", 0), 0u) << in.upper;   // sells once back up at the mean
    EXPECT_EQ(in.state.rfind("long, z -", 0), 0u) << in.state;
}

TEST(LevelsTests, MacdLineToCross)
{
    MACD macd(StrategyAttribute(json{{"name", "MACD"}, {"short_period", 3}, {"long_period", 6}, {"signal_period", 3},
                                     {"signal_type", "zero_cross"}}));
    std::vector<float> rising;
    for (int i = 0; i < 60; i++) rising.push_back(100.0f + i);
    MarketData data = bars(rising);
    run(macd, data);
    auto l = macd.decisionLevels();
    EXPECT_EQ(l.label, "MACD(3/6/3)");
    EXPECT_EQ(l.lower, "0.0000");   // above the zero line: it would be crossed going down
    EXPECT_EQ(l.upper, "");
    EXPECT_EQ(l.state, "above 0");
}

TEST(LevelsTests, ScalpBandsAroundVwap)
{
    SCALP scalp(StrategyAttribute(json{{"name", "SCALP"}, {"entry_threshold", 2.0}, {"exit_threshold", 0.0},
                                       {"max_hold_bars", 10}, {"stop_loss", 0.3}, {"take_profit", 1.0}}));
    std::vector<float> closes;
    for (int i = 0; i < 20; i++) closes.push_back(i % 2 ? 100.1f : 99.9f);
    MarketData data = bars(closes);
    run(scalp, data);
    auto l = scalp.decisionLevels();
    EXPECT_EQ(l.label, "SCALP");
    EXPECT_EQ(l.lower, "99.80");
    EXPECT_EQ(l.upper, "100.20");
    EXPECT_EQ(l.state, "flat, VWAP 100.00");
}

TEST(LevelsTests, EngineReportsWarmingUp)
{
    json config = {{"ticker", "TEST"}, {"max_position_size", 10}, {"max_exposure", 5.0}, {"slippage_tolerance", 2.0},
                   {"strategies", {{{"name", "RSI"}, {"active", 1}, {"period", 14}, {"oversold_threshold", 30},
                                    {"overbought_threshold", 70}, {"stop_loss", 5}, {"take_profit", 5}}}}};
    StrategyFactory factory(config);
    StrategyEngine engine;
    MarketData data = bars({100, 101, 102});
    engine.setUp(config, factory, data);
    engine.setPrintStatus(false);
    auto levels = engine.decisionLevels();
    ASSERT_EQ(levels.size(), 1u);
    EXPECT_EQ(levels[0].label, "RSI(14)");
    EXPECT_FALSE(levels[0].ready);
    EXPECT_EQ(levels[0].state, "warming up: 3/14 bars");
}
