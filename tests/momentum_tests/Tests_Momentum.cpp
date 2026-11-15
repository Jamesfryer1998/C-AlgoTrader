#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include "momentum/MomentumDay.hpp"
#include "momentum/MomentumSettings.hpp"
#include "momentum/MomentumTrader.hpp"

namespace
{
    const int MINUTES = 390;
    const double PRICE = 100;
    const double CAPITAL = 100000;

    std::string timeOf(int minute)
    {
        int clock = 9 * 60 + 30 + minute;
        char text[40];
        std::snprintf(text, sizeof(text), "2026-01-05 %02d:%02d:00", clock / 60, clock % 60);
        return text;
    }

    // A minute bar at each close, opening at the close before (the first at `open`)
    MomentumDay dayOf(const std::vector<double>& closes, double open, double volume = 1000)
    {
        std::vector<IntradayBar> bars;
        for (int minute = 0; minute < static_cast<int>(closes.size()); minute++)
        {
            double from = minute == 0 ? open : closes[minute - 1];
            bars.push_back({timeOf(minute), from, std::max(from, closes[minute]), std::min(from, closes[minute]),
                            closes[minute], volume});
        }
        return MomentumDay(bars);
    }

    // A quiet day: 0.1% up and down around `level`
    MomentumDay quiet(double level)
    {
        std::vector<double> closes;
        for (int minute = 0; minute < MINUTES; minute++)
            closes.push_back(level * (1 + 0.001 * std::sin(minute / 10.0)));
        return dayOf(closes, level);
    }

    TradingCosts noFees()
    {
        TradingCosts costs;
        costs.regulatoryFees = RegulatoryFees();
        return costs;
    }

    MomentumSettings::Variant published()
    {
        MomentumSettings::Variant variant;
        variant.name = "noise area";
        variant.lookbackDays = 3;
        variant.targetVolatility = 0;   // a fixed size, to check the sums
        variant.maxLeverage = 1.0;
        variant.costPerShare = 0;
        return variant;
    }

    // Warm a trader up on quiet days, so its band is about 0.1%
    void warmUp(MomentumTrader& trader, int days)
    {
        MorningBacktest::Line line;
        std::vector<MomentumTrader::Trade> trades;
        for (int day = 0; day < days; day++)
            trader.day(quiet(PRICE), "warm-up", CAPITAL, false, line, trades);
    }
}

TEST(MomentumDayTests, FillsQuietMinutesAndKeepsAVwap)
{
    std::vector<IntradayBar> bars = {{timeOf(0), 10, 11, 9, 10, 100}, {timeOf(2), 12, 13, 11, 12, 300},
                                     {timeOf(389), 12, 12, 12, 12, 10}};
    MomentumDay day(bars);
    ASSERT_TRUE(day.usable());
    EXPECT_EQ(day.priceAfter(1), 10);
    EXPECT_EQ(day.fillAt(2), 12);
    EXPECT_EQ(day.fillAt(1), 10);     // no bar: the last price
    EXPECT_NEAR(day.vwapThrough(2), (10.0 * 100 + 12.0 * 300) / 400, 1e-9);
    EXPECT_NEAR(day.movesFromOpen()[2], 0.2, 1e-12);
    EXPECT_FALSE(MomentumDay(std::vector<IntradayBar>()).usable());
}

TEST(MomentumTraderTests, AveragesTheMovesAndMeasuresVolatility)
{
    std::deque<std::vector<double>> days = {std::vector<double>(MINUTES, 0.01), std::vector<double>(MINUTES, 0.03)};
    EXPECT_NEAR(MomentumTrader::averageMoves(days)[100], 0.02, 1e-12);
    // Returns +1%, -0.99%, +1.01%: the sample standard deviation
    double a = 0.01, b = 100.0 / 101 - 1, c = 0.01, mean = (a + b + c) / 3;
    double expected = std::sqrt(((a - mean) * (a - mean) + (b - mean) * (b - mean) + (c - mean) * (c - mean)) / 2);
    EXPECT_NEAR(MomentumTrader::volatility({100, 101, 100, 101}), expected, 1e-12);
    EXPECT_EQ(MomentumTrader::volatility({100}), 0);
}

TEST(MomentumTraderTests, BuysABreakoutAboveTheBandAndHoldsItToTheClose)
{
    MomentumTrader trader(published(), "SPY", noFees());
    warmUp(trader, 5);
    // A trend day: up 2% steadily, far beyond a 0.1% band
    std::vector<double> closes;
    for (int minute = 0; minute < MINUTES; minute++)
        closes.push_back(PRICE * (1 + 0.02 * minute / (MINUTES - 1)));
    MorningBacktest::Line line;
    std::vector<MomentumTrader::Trade> trades;
    double pnl = trader.day(dayOf(closes, PRICE), "2026-01-12", CAPITAL, true, line, trades);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].side, 1);
    EXPECT_NEAR(trades[0].entry, closes[29], 1e-9);   // at 10:00, at minute 30's open
    EXPECT_NEAR(trades[0].exit, closes.back(), 1e-9);
    double shares = std::floor(CAPITAL / trades[0].entry);
    EXPECT_NEAR(pnl, shares * (closes.back() - closes[29]), 1e-6);
    EXPECT_EQ(line.stockDays.size(), 1u);
}

TEST(MomentumTraderTests, ShortsABreakdownAndStopsAtTheVwap)
{
    MomentumTrader trader(published(), "SPY", noFees());
    warmUp(trader, 5);
    // Down 2% by 11:00 (heavy volume), then back up to the open by the close
    std::vector<double> closes;
    for (int minute = 0; minute < MINUTES; minute++)
        closes.push_back(minute <= 90 ? PRICE * (1 - 0.02 * minute / 90.0) : PRICE * (0.98 + 0.02 * (minute - 90) / 299.0));
    MorningBacktest::Line line;
    std::vector<MomentumTrader::Trade> trades;
    trader.day(dayOf(closes, PRICE), "2026-01-12", CAPITAL, true, line, trades);
    ASSERT_FALSE(trades.empty());
    EXPECT_EQ(trades[0].side, -1);
    EXPECT_LT(trades[0].exit, PRICE);   // out on the way back up, through the VWAP, before the band
}

TEST(MomentumTraderTests, NoTradesUntilWarmedUpAndNoneOnAQuietDay)
{
    MomentumTrader trader(published(), "SPY", noFees());
    MorningBacktest::Line line;
    std::vector<MomentumTrader::Trade> trades;
    std::vector<double> closes(MINUTES, PRICE * 1.05);
    trader.day(dayOf(closes, PRICE), "2026-01-05", CAPITAL, true, line, trades);
    EXPECT_TRUE(trades.empty());   // no history yet
    warmUp(trader, 5);
    trader.day(quiet(PRICE), "2026-01-12", CAPITAL, true, line, trades);
    EXPECT_TRUE(trades.empty());   // never outside its usual range
}

TEST(MomentumTraderTests, OvernightBuysTheCloseAndSellsTheNextOpen)
{
    MomentumSettings::Variant variant = published();
    variant.kind = MomentumSettings::Kind::Overnight;
    MomentumTrader trader(variant, "SPY", noFees());
    warmUp(trader, 5);
    MorningBacktest::Line line;
    std::vector<MomentumTrader::Trade> trades;
    trader.day(quiet(PRICE), "2026-01-12", CAPITAL, true, line, trades);
    EXPECT_TRUE(trades.empty());   // bought at the close; nothing sold yet
    double pnl = trader.day(quiet(PRICE * 1.01), "2026-01-13", CAPITAL, true, line, trades);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_NEAR(trades[0].exit, PRICE * 1.01, 1e-9);
    EXPECT_GT(pnl, 0);
}

TEST(MomentumTraderTests, TheControlReplaysTheSameTradesInRandomDirections)
{
    MomentumTrader trader(published(), "SPY", noFees());
    std::vector<MomentumTrader::Trade> trades(200, MomentumTrader::Trade{100, 101, 1, 1.0});
    std::mt19937_64 random(3);
    MorningBacktest::Line line;
    double pnl = trader.replay(trades, "2026-01-12", CAPITAL, random, line);
    ASSERT_EQ(line.stockDays.size(), 200u);
    int longs = 0;
    for (const auto& trade : line.stockDays)
        longs += trade.grossPnl > 0 ? 1 : 0;
    EXPECT_GT(longs, 70);
    EXPECT_LT(longs, 130);
    EXPECT_LT(std::fabs(pnl), 200 * 1000.0 * 0.5);
}
