#include <gtest/gtest.h>
#include <cmath>
#include "quant/DeflatedSharpe.hpp"

TEST(DeflatedSharpeTests, NormalDistributionBothWays)
{
    EXPECT_NEAR(quant::normalCdf(0), 0.5, 1e-12);
    EXPECT_NEAR(quant::normalCdf(1.959964), 0.975, 1e-6);
    EXPECT_NEAR(quant::normalQuantile(0.975), 1.959964, 1e-5);
    EXPECT_NEAR(quant::normalQuantile(0.01), -2.326348, 1e-5);   // the low tail's own formula
    EXPECT_NEAR(quant::normalQuantile(0.999), 3.090232, 1e-5);
}

TEST(DeflatedSharpeTests, SkewAndKurtosisOfKnownShapes)
{
    quant::Vector symmetric = {-2, -1, 0, 1, 2};
    EXPECT_NEAR(quant::skewness(symmetric), 0, 1e-12);
    EXPECT_NEAR(quant::kurtosis(symmetric), 1.7, 1e-12);   // flat: thinner tails than a normal's 3
    quant::Vector rightTail = {0, 0, 0, 0, 10};
    EXPECT_GT(quant::skewness(rightTail), 1);
}

TEST(DeflatedSharpeTests, MoreTrialsRaiseTheBarForLuck)
{
    EXPECT_DOUBLE_EQ(quant::expectedMaxSharpe(0.01, 1), 0);
    double fromTwo = quant::expectedMaxSharpe(0.01, 2);
    double fromHundred = quant::expectedMaxSharpe(0.01, 100);
    EXPECT_GT(fromTwo, 0);
    EXPECT_GT(fromHundred, fromTwo);
    EXPECT_NEAR(quant::expectedMaxSharpe(0.04, 100), 2 * fromHundred, 1e-12) << "scales with the spread of Sharpes";
}

TEST(DeflatedSharpeTests, AClearEdgeSurvivesALuckyLookingOneDoesNot)
{
    // 500 days averaging +0.2% with a 1% standard deviation: a daily Sharpe of about 0.2
    quant::Vector steady;
    for (int day = 0; day < 500; day++)
        steady.push_back(0.002 + (day % 2 ? 0.01 : -0.01));
    double sharpe = quant::dailySharpe(steady);
    EXPECT_NEAR(sharpe, 0.2, 0.01);

    EXPECT_GT(quant::deflatedSharpe(steady, {sharpe}), 0.99) << "one trial: plainly real";

    // Tried alongside many strategies whose Sharpes spread widely: much less convincing
    quant::Vector manyTrials;
    for (int i = 0; i < 200; i++)
        manyTrials.push_back((i - 100) * 0.003);
    double deflated = quant::deflatedSharpe(steady, manyTrials);
    EXPECT_LT(deflated, quant::deflatedSharpe(steady, {sharpe}));

    quant::Vector shortRun(steady.begin(), steady.begin() + 20);
    EXPECT_LT(quant::deflatedSharpe(shortRun, {sharpe}), 0.95) << "20 days isn't enough to tell";
}
