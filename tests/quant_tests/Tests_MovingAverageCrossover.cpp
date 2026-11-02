#include <gtest/gtest.h>
#include <cmath>
#include "Synthetic.hpp"
#include "quant/MovingAverageCrossover.hpp"

using namespace quant;

TEST(MovingAverageCrossoverTests, SimpleMovingAverage)
{
    Vector sma = MovingAverageCrossover::simpleMovingAverage({1, 2, 3, 4, 5}, 3);
    EXPECT_TRUE(std::isnan(sma[0]));
    EXPECT_TRUE(std::isnan(sma[1]));
    EXPECT_DOUBLE_EQ(sma[2], 2.0);
    EXPECT_DOUBLE_EQ(sma[4], 4.0);
}

TEST(MovingAverageCrossoverTests, FallThenRiseBuysOnceAfterTheTurn)
{
    // 300 days down, then 300 days up
    Vector closes;
    for (int i = 0; i < 300; i++) closes.push_back(200.0 - 0.3 * i);
    for (int i = 0; i < 300; i++) closes.push_back(110.0 + 0.3 * i);
    auto dates = synthetic::weekdays(closes.size());

    CrossoverSettings settings;
    settings.costBps = 10;
    CrossoverResult r = MovingAverageCrossover::backtest(dates, closes, settings);

    EXPECT_EQ(r.trades, 1);
    EXPECT_EQ(r.position.front(), 0.0);   // in cash during the fall
    EXPECT_EQ(r.position.back(), 1.0);    // invested by the end of the rise
    EXPECT_GT(r.strategy.totalReturn, 0.0);
    EXPECT_GT(r.strategy.totalReturn, r.buyHold.totalReturn);   // sat out the fall
    EXPECT_LT(r.strategy.maxDrawdown, r.buyHold.maxDrawdown);
    EXPECT_NEAR(r.costsPaid, 0.001, 1e-12);   // one buy at 10 bps

    // The backtest starts the day after the 200-day average first exists
    EXPECT_EQ(r.dates.size(), closes.size() - 200);
    EXPECT_EQ(r.dates.front(), dates[200]);
}

TEST(MovingAverageCrossoverTests, NoLookAhead)
{
    // A crash on the last day can't change any position (they only use earlier closes)...
    Vector closes = synthetic::randomWalk(800, 0.012, 3, 100.0, 0.0003);
    auto dates = synthetic::weekdays(closes.size());
    CrossoverSettings settings;
    CrossoverResult before = MovingAverageCrossover::backtest(dates, closes, settings);
    closes.back() *= 0.5;
    CrossoverResult after = MovingAverageCrossover::backtest(dates, closes, settings);
    EXPECT_EQ(before.position, after.position);

    // ...and the position each day is the signal from the previous close
    for (size_t i = 1; i < before.position.size(); i++)
    {
        bool signalYesterday = before.fastAverage[i - 1] > before.slowAverage[i - 1];
        EXPECT_EQ(before.position[i], signalYesterday ? 1.0 : 0.0) << "day " << i;
    }
}

TEST(MovingAverageCrossoverTests, CostsAndLookAheadFlatterTheResult)
{
    Vector closes = synthetic::randomWalk(2000, 0.012, 8, 100.0, 0.0002);
    auto dates = synthetic::weekdays(closes.size());
    CrossoverSettings settings;
    settings.fastDays = 10;   // fast averages trade often, so costs and look-ahead matter
    settings.slowDays = 30;
    settings.costBps = 10;
    CrossoverResult r = MovingAverageCrossover::backtest(dates, closes, settings);

    EXPECT_GT(r.trades, 10);
    EXPECT_GT(r.strategyNoCosts.totalReturn, r.strategy.totalReturn);
    EXPECT_GT(r.lookAhead.totalReturn, r.strategy.totalReturn);   // trading on today's close is "free" profit
    EXPECT_NEAR(r.costsPaid, 0.001 * (2 * r.trades - (r.position.back() == 1.0 ? 1 : 0)), 1e-9);
}

TEST(MovingAverageCrossoverTests, BuyAndHoldOnTheSameDays)
{
    Vector closes;
    for (int i = 0; i < 260; i++) closes.push_back(100.0 * std::pow(1.001, i));
    auto dates = synthetic::weekdays(closes.size());
    CrossoverSettings settings;
    settings.costBps = 0;
    CrossoverResult r = MovingAverageCrossover::backtest(dates, closes, settings);
    EXPECT_NEAR(r.buyHold.totalReturn, closes.back() / closes[199] - 1.0, 1e-9);
}

TEST(MovingAverageCrossoverTests, RejectsBadSettings)
{
    Vector closes(300, 100.0);
    auto dates = synthetic::weekdays(300);
    CrossoverSettings backwards;
    backwards.fastDays = 200;
    backwards.slowDays = 50;
    EXPECT_THROW(MovingAverageCrossover::backtest(dates, closes, backwards), std::runtime_error);
    EXPECT_THROW(MovingAverageCrossover::backtest(dates, Vector(150, 100.0), CrossoverSettings()), std::runtime_error);
}
