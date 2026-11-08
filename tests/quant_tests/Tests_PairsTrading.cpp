#include <gtest/gtest.h>
#include <cmath>
#include "Synthetic.hpp"
#include "quant/PairsTrading.hpp"

using namespace quant;

namespace
{
    // X wanders randomly; Y = X^1.2 times a mean-reverting wobble -> a cointegrated pair
    struct Pair
    {
        std::vector<std::string> dates;
        Vector y, x;
    };

    Pair cointegratedPair(size_t days = 1000, double wobble = 0.03)
    {
        Pair p;
        p.dates = synthetic::weekdays(days);
        p.x = synthetic::randomWalk(days, 0.012, 21, 60.0);
        Vector spread = synthetic::ar1(days, 0.9, wobble * std::sqrt(1 - 0.81), 22);
        for (size_t i = 0; i < days; i++)
            p.y.push_back(std::exp(0.5 + 1.2 * std::log(p.x[i]) + spread[i]));
        return p;
    }
}

TEST(PairsTradingTests, TradesACointegratedPairProfitably)
{
    Pair p = cointegratedPair();
    PairsSettings settings;
    settings.costBps = 5;
    PairsResult r = PairsTrading::backtest("YYY", "XXX", p.dates, p.y, p.x, settings);

    EXPECT_TRUE(r.fullSample.cointegrated());
    EXPECT_NEAR(r.fullSample.beta, 1.2, 0.05);
    EXPECT_GE(r.trades.size(), 5u);
    EXPECT_GT(r.performance.totalReturn, 0.0);
    EXPECT_GT(r.performance.sharpe, 1.0);

    int reverted = 0;
    for (const auto& t : r.trades)
    {
        EXPECT_TRUE(t.side == 1 || t.side == -1);
        EXPECT_FALSE(t.entryDate.empty());
        if (t.exitReason == "reverted") reverted++;
    }
    EXPECT_GT(reverted, static_cast<int>(r.trades.size()) / 2);

    // Backtest starts once the first lookback window is full, plus the 1-day delay
    EXPECT_EQ(r.dates.size(), p.dates.size() - settings.lookbackDays);
    EXPECT_EQ(r.dates.front(), p.dates[settings.lookbackDays]);
}

TEST(PairsTradingTests, EntriesFollowTheZScoreRules)
{
    Pair p = cointegratedPair();
    PairsSettings settings;
    PairsResult r = PairsTrading::backtest("YYY", "XXX", p.dates, p.y, p.x, settings);

    for (const auto& t : r.trades)
    {
        // Stretched up -> short the spread; stretched down -> long it
        EXPECT_LT(std::fabs(t.entryZ), settings.stopZ);   // never opened beyond the stop level
        if (t.side == -1)
        {
            EXPECT_GT(t.entryZ, settings.entryZ);
        }
        else
        {
            EXPECT_LT(t.entryZ, -settings.entryZ);
        }
    }
}

TEST(PairsTradingTests, NoLookAhead)
{
    // A jump on the last day can't change any position: positions only use earlier closes
    Pair p = cointegratedPair();
    PairsSettings settings;
    PairsResult before = PairsTrading::backtest("YYY", "XXX", p.dates, p.y, p.x, settings);
    p.y.back() *= 1.10;   // stretches the spread past the entry level (but not the stop)
    PairsResult after = PairsTrading::backtest("YYY", "XXX", p.dates, p.y, p.x, settings);
    EXPECT_EQ(before.position, after.position);

    // With no delay (look-ahead) the same jump can move positions decided on that last close
    settings.signalDelayDays = 0;
    PairsResult peeking = PairsTrading::backtest("YYY", "XXX", p.dates, p.y, p.x, settings);
    EXPECT_EQ(peeking.dates.size(), before.dates.size() + 1);   // starts a day earlier
    EXPECT_NE(peeking.position.back(), 0.0);                    // the jump stretched the spread on the day itself
}

TEST(PairsTradingTests, CostsReduceReturns)
{
    Pair p = cointegratedPair();
    PairsSettings free, expensive;
    free.costBps = 0;
    expensive.costBps = 50;
    PairsResult a = PairsTrading::backtest("YYY", "XXX", p.dates, p.y, p.x, free);
    PairsResult b = PairsTrading::backtest("YYY", "XXX", p.dates, p.y, p.x, expensive);
    EXPECT_EQ(a.trades.size(), b.trades.size());
    EXPECT_GT(a.performance.totalReturn, b.performance.totalReturn);
}

TEST(PairsTradingTests, StopCutsADivergingSpread)
{
    // Cointegrated for 600 days, then Y drifts away for good: the relationship breaks
    Pair p = cointegratedPair(900);
    for (size_t i = 600; i < p.y.size(); i++)
        p.y[i] *= std::exp(0.004 * (i - 599));

    PairsSettings settings;
    settings.stopZ = 4.0;
    PairsResult r = PairsTrading::backtest("YYY", "XXX", p.dates, p.y, p.x, settings);
    bool stopped = false;
    for (const auto& t : r.trades)
        stopped = stopped || t.exitReason == "stop";
    EXPECT_TRUE(stopped);
}

TEST(PairsTradingTests, YearByYearAndScan)
{
    Pair p = cointegratedPair(800);   // weekdays from 2018: 3 full years and a part one
    auto years = PairsTrading::testByYear(p.dates, p.y, p.x);
    ASSERT_EQ(years.size(), 3u);
    EXPECT_EQ(years[0].first, "2018");

    PriceTable table;
    table.symbols = {"YYY", "XXX", "ZZZ"};
    table.dates = p.dates;
    table.closes = {p.y, p.x, synthetic::randomWalk(800, 0.012, 77)};
    std::vector<PairScan> scan = PairsTrading::scan(table);
    ASSERT_EQ(scan.size(), 3u);
    EXPECT_EQ(scan[0].y, "YYY");   // the real pair ranks first
    EXPECT_EQ(scan[0].x, "XXX");
    EXPECT_TRUE(scan[0].test.cointegrated());
}

TEST(PairsTradingTests, RejectsTooLittleData)
{
    Pair p = cointegratedPair(100);
    EXPECT_THROW(PairsTrading::backtest("YYY", "XXX", p.dates, p.y, p.x, PairsSettings()), std::runtime_error);
}
