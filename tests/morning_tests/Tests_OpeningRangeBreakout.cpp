#include <gtest/gtest.h>
#include "MorningTestBars.hpp"
#include "morning/OpeningRangeBreakout.hpp"

using namespace MorningTestBars;

namespace
{
    const std::string DAY = "2026-03-02";
    const json FLAT_AT_CLOSE = {{"end_of_day", {{"flat_at_close", true}, {"no_entries_minutes_before_close", 15},
                                                 {"close_minutes_before_close", 5}}}};
    const double EQUITY = 10000;

    TradingCosts noCosts()
    {
        TradingCosts costs;
        costs.slippagePercent = 0;
        costs.regulatoryFees = RegulatoryFees();
        return costs;
    }

    // An up opening range 100-101 (09:30-09:34), then flat at 100.5 for the rest of the day
    std::vector<IntradayBar> upOpening()
    {
        std::vector<IntradayBar> bars = flatDay(DAY, 100.5);
        setBar(bars, minute(9, 30), 100, 100.6, 100, 100.4);
        setBar(bars, minute(9, 34), 100.4, 101, 100.3, 100.8);
        return bars;
    }

    StockSelection::Pick longPick(double atr = 2.0)
    {
        return {"KO", atr, 3.0, 1};
    }

    std::optional<MorningTrade> trade(const std::vector<IntradayBar>& bars, MorningSettings::Variant variant = {},
                                      MorningSettings settings = simpleSettings(), TradingCosts costs = noCosts(),
                                      StockSelection::Pick pick = longPick())
    {
        OpeningRangeBreakout breakout(settings, variant, costs, DayEnd(FLAT_AT_CLOSE));
        return breakout.trade(pick, bars, EQUITY);
    }
}

TEST(OpeningRangeBreakoutTests, NoBreakoutNoTrade)
{
    EXPECT_FALSE(trade(upOpening()).has_value());
}

TEST(OpeningRangeBreakoutTests, BuysAtTheNextOpenAfterTheBreakoutAndHoldsToTheClose)
{
    std::vector<IntradayBar> bars = upOpening();
    setBar(bars, minute(10, 0), 100.6, 101.3, 100.6, 101.2);   // through 101
    setBar(bars, minute(10, 1), 101.4, 101.5, 101.3, 101.4);   // next open 101.4: worse than the level
    for (int m = minute(10, 2); m < minute(16, 0); m++)
        setBar(bars, m, 101.5, 101.6, 101.4, 101.5);
    setBar(bars, minute(15, 54), 101.5, 101.9, 101.5, 101.8);

    auto result = trade(bars);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->side, 1);
    EXPECT_EQ(result->entryTime, at(DAY, minute(10, 1)));
    EXPECT_DOUBLE_EQ(result->entryPrice, 101.4);
    EXPECT_DOUBLE_EQ(result->stopPrice, 101.4 - 0.1 * 2.0);
    EXPECT_EQ(result->exitReason, OpeningRangeBreakout::CLOSE);
    EXPECT_EQ(result->exitTime, at(DAY, minute(15, 54)));   // the bar ending 15:55
    EXPECT_DOUBLE_EQ(result->exitPrice, 101.8);
    // 1% of $10,000 at $0.20 a share = 500 shares, capped at 4 x $10,000 / 2 stocks = $20,000: 197 at 101.4
    EXPECT_DOUBLE_EQ(result->shares, 197);
    EXPECT_NEAR(result->netPnl, 197 * 0.4, 1e-6);
    EXPECT_NEAR(result->r(), 0.4 / 0.2, 1e-9);
}

TEST(OpeningRangeBreakoutTests, TouchFillsAtTheLevelOrABetterOpen)
{
    std::vector<IntradayBar> bars = upOpening();
    setBar(bars, minute(10, 0), 100.6, 101.3, 100.6, 101.2);
    MorningSettings::Variant touch;
    touch.entryFill = MorningSettings::EntryFill::Touch;
    auto result = trade(bars, touch);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->entryTime, at(DAY, minute(10, 0)));
    EXPECT_DOUBLE_EQ(result->entryPrice, 101.0);

    setBar(bars, minute(10, 0), 101.2, 101.3, 101.1, 101.2);   // opened above the level: filled at the open
    EXPECT_DOUBLE_EQ(trade(bars, touch)->entryPrice, 101.2);
}

TEST(OpeningRangeBreakoutTests, TheStopFillsAtTheStopOrAWorseOpen)
{
    std::vector<IntradayBar> bars = upOpening();
    setBar(bars, minute(10, 0), 100.6, 101.3, 100.6, 101.2);
    for (int m = minute(10, 1); m < minute(10, 5); m++)
        setBar(bars, m, 101.0, 101.1, 101.0, 101.0);           // entry 101.0 at 10:01, stop 100.8
    setBar(bars, minute(10, 5), 100.9, 100.9, 100.7, 100.8);   // touches 100.8
    auto result = trade(bars);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->exitReason, OpeningRangeBreakout::STOP);
    EXPECT_DOUBLE_EQ(result->exitPrice, 100.8);
    EXPECT_NEAR(result->r(), -1.0, 1e-9);

    setBar(bars, minute(10, 5), 100.5, 100.6, 100.4, 100.5);   // gapped through: filled at the open
    EXPECT_DOUBLE_EQ(trade(bars)->exitPrice, 100.5);
}

TEST(OpeningRangeBreakoutTests, ShortsADownOpening)
{
    std::vector<IntradayBar> bars = flatDay(DAY, 99.5);
    setBar(bars, minute(9, 30), 100, 100.2, 99.4, 99.6);
    setBar(bars, minute(9, 34), 99.6, 99.7, 99.0, 99.2);       // range low 99.0, closed down
    setBar(bars, minute(10, 0), 99.3, 99.3, 98.8, 98.9);       // through 99.0
    setBar(bars, minute(10, 1), 98.9, 98.9, 98.8, 98.85);      // entry at 98.9
    StockSelection::Pick shortPick = {"KO", 2.0, 3.0, -1};
    auto result = trade(bars, {}, simpleSettings(), noCosts(), shortPick);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->side, -1);
    EXPECT_DOUBLE_EQ(result->entryPrice, 98.9);
    EXPECT_DOUBLE_EQ(result->stopPrice, 99.1);
    // 99.5 at 10:02 is above the stop: stopped out at the 10:02 open
    EXPECT_EQ(result->exitReason, OpeningRangeBreakout::STOP);
    EXPECT_DOUBLE_EQ(result->exitPrice, 99.5);
}

TEST(OpeningRangeBreakoutTests, ShortHoldsAndTargetsExitEarly)
{
    std::vector<IntradayBar> bars = upOpening();
    setBar(bars, minute(10, 0), 100.6, 101.3, 100.6, 101.2);
    setBar(bars, minute(10, 1), 101.0, 101.1, 101.0, 101.0);   // entry 101.0, risk 0.2
    for (int m = minute(10, 2); m < minute(16, 0); m++)
        setBar(bars, m, 101.1, 101.15, 101.05, 101.1);
    setBar(bars, minute(10, 20), 101.2, 101.5, 101.2, 101.45);   // reaches 2R = 101.4

    MorningSettings::Variant tenMinutes;
    tenMinutes.holdMinutes = 10;
    auto timed = trade(bars, tenMinutes);
    ASSERT_TRUE(timed.has_value());
    EXPECT_EQ(timed->exitReason, OpeningRangeBreakout::TIME);
    EXPECT_EQ(timed->exitTime, at(DAY, minute(10, 10)));   // 10:01 to the end of the 10:10 bar

    MorningSettings::Variant twoR;
    twoR.targetR = 2.0;
    auto target = trade(bars, twoR);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->exitReason, OpeningRangeBreakout::TARGET);
    EXPECT_DOUBLE_EQ(target->exitPrice, 101.4);
}

TEST(OpeningRangeBreakoutTests, NothingOpensFrom1545)
{
    std::vector<IntradayBar> bars = upOpening();
    setBar(bars, minute(15, 44), 100.6, 101.3, 100.6, 101.2);   // the bar ending 15:45
    EXPECT_FALSE(trade(bars).has_value());
}

TEST(OpeningRangeBreakoutTests, CostsAreSlippageOnBothFillsAndFeesOnTheSale)
{
    std::vector<IntradayBar> bars = upOpening();
    setBar(bars, minute(10, 0), 100.6, 101.3, 100.6, 101.2);
    setBar(bars, minute(10, 1), 101.0, 101.1, 101.0, 101.0);
    TradingCosts costs = noCosts();
    costs.slippagePercent = 0.03;
    costs.regulatoryFees = RegulatoryFees(json::object());
    auto result = trade(bars, {}, simpleSettings(), costs);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->entryPrice, 101.0 * 1.0003, 1e-9);
    EXPECT_NEAR(result->exitPrice, 100.5 * 0.9997, 1e-9);
    double slippage = result->shares * (101.0 * 0.0003 + 100.5 * 0.0003);
    double fees = costs.regulatoryFees.onSale(result->shares, result->exitPrice);
    EXPECT_NEAR(result->costs, slippage + fees, 1e-6);
    EXPECT_NEAR(result->netPnl, result->grossPnl - result->costs, 1e-9);
    EXPECT_GT(fees, 0);
}
