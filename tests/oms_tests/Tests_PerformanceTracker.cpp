#include <gtest/gtest.h>
#include <cmath>
#include "../../src/oms/PerformanceTracker.hpp"

TEST(PerformanceTrackerTests, StartsFlat)
{
    PerformanceTracker tracker(10000);
    PerformanceSummary s = tracker.summary();
    EXPECT_DOUBLE_EQ(s.equity, 10000);
    EXPECT_DOUBLE_EQ(s.totalPnL, 0);
    EXPECT_DOUBLE_EQ(s.roiPercent, 0);
    EXPECT_EQ(s.closedTrades, 0);
}

TEST(PerformanceTrackerTests, OpenLongIsUnrealised)
{
    PerformanceTracker tracker(10000);
    tracker.recordFill("NVDA", true, 10, 100);   // buy 10 @ 100
    tracker.markPrice("NVDA", 105);

    PerformanceSummary s = tracker.summary();
    EXPECT_DOUBLE_EQ(s.realisedPnL, 0);
    EXPECT_DOUBLE_EQ(s.unrealisedPnL, 50);       // 10 x (105 - 100)
    EXPECT_DOUBLE_EQ(s.equity, 10050);
    EXPECT_DOUBLE_EQ(s.roiPercent, 0.5);
}

TEST(PerformanceTrackerTests, AverageCostAndPartialClose)
{
    PerformanceTracker tracker(10000);
    tracker.recordFill("NVDA", true, 10, 100);   // avg 100
    tracker.recordFill("NVDA", true, 10, 110);   // avg 105
    tracker.recordFill("NVDA", false, 5, 120);   // realise 5 x (120 - 105) = 75
    tracker.markPrice("NVDA", 120);

    PerformanceSummary s = tracker.summary();
    EXPECT_DOUBLE_EQ(s.realisedPnL, 75);
    EXPECT_DOUBLE_EQ(s.unrealisedPnL, 225);      // 15 left x (120 - 105)
    EXPECT_DOUBLE_EQ(tracker.getPositionQuantity("NVDA"), 15);
    EXPECT_EQ(s.closedTrades, 1);
    EXPECT_EQ(s.winningTrades, 1);
}

TEST(PerformanceTrackerTests, ShortProfitsWhenPriceFalls)
{
    PerformanceTracker tracker(10000);
    tracker.recordFill("NVDA", false, 4, 200);   // short 4 @ 200
    tracker.recordFill("NVDA", true, 4, 190);    // cover @ 190: +40

    PerformanceSummary s = tracker.summary();
    EXPECT_DOUBLE_EQ(s.realisedPnL, 40);
    EXPECT_DOUBLE_EQ(s.unrealisedPnL, 0);
    EXPECT_DOUBLE_EQ(s.equity, 10040);
}

TEST(PerformanceTrackerTests, FlipFromLongToShort)
{
    PerformanceTracker tracker(10000);
    tracker.recordFill("NVDA", true, 2, 100);
    tracker.recordFill("NVDA", false, 5, 90);    // close 2 (-20), open short 3 @ 90
    tracker.markPrice("NVDA", 80);

    PerformanceSummary s = tracker.summary();
    EXPECT_DOUBLE_EQ(s.realisedPnL, -20);
    EXPECT_DOUBLE_EQ(tracker.getPositionQuantity("NVDA"), -3);
    EXPECT_DOUBLE_EQ(s.unrealisedPnL, 30);       // short 3 from 90 to 80
    EXPECT_EQ(s.losingTrades, 1);
}

TEST(PerformanceTrackerTests, CommissionReducesEquityNotRealised)
{
    PerformanceTracker tracker(10000);
    tracker.recordFill("NVDA", true, 1, 100, 1.0);
    tracker.recordFill("NVDA", false, 1, 100, 1.0);

    PerformanceSummary s = tracker.summary();
    EXPECT_DOUBLE_EQ(s.realisedPnL, 0);
    EXPECT_DOUBLE_EQ(s.commissionPaid, 2);
    EXPECT_DOUBLE_EQ(s.totalPnL, -2);
}

TEST(PerformanceTrackerTests, DrawdownFromPeak)
{
    PerformanceTracker tracker(1000);
    tracker.recordFill("X", true, 10, 100);      // all-in
    for (double price : {100.0, 110.0, 99.0, 105.0}) {
        tracker.markPrice("X", price);
        tracker.recordEquity();
    }
    // equity 1000 -> 1100 -> 990: drawdown 110 / 1100 = 10%
    EXPECT_NEAR(tracker.summary().maxDrawdownPercent, 10.0, 1e-9);
}

TEST(PerformanceTrackerTests, SharpeIsAnnualisedByBarsPerYear)
{
    // Alternating +1% / -0.5% returns
    PerformanceTracker daily(1000, 252);
    PerformanceTracker minute(1000, PerformanceTracker::MINUTE_BARS_PER_YEAR);
    for (PerformanceTracker* t : {&daily, &minute}) {
        t->recordFill("X", true, 10, 100);
        double price = 100;
        for (int i = 0; i < 20; i++) {
            price *= (i % 2 == 0) ? 1.01 : 0.995;
            t->markPrice("X", price);
            t->recordEquity();
        }
    }
    double dailySharpe = daily.summary().sharpeRatio;
    EXPECT_GT(dailySharpe, 0);
    EXPECT_NEAR(minute.summary().sharpeRatio / dailySharpe, std::sqrt(390.0), 1e-6);
}
