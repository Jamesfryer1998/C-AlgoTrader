#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include "Synthetic.hpp"
#include "quant/DailyBars.hpp"
#include "quant/Stats.hpp"

using namespace quant;

TEST(QuantStatsTests, MeanStdevAndReturns)
{
    Vector v = {1, 2, 3, 4};
    EXPECT_DOUBLE_EQ(mean(v), 2.5);
    EXPECT_NEAR(stdev(v), std::sqrt(5.0 / 3.0), 1e-12);

    Vector prices = {100, 110, 99};
    Vector simple = simpleReturns(prices);
    ASSERT_EQ(simple.size(), 2u);
    EXPECT_NEAR(simple[0], 0.10, 1e-12);
    EXPECT_NEAR(simple[1], -0.10, 1e-12);
    EXPECT_NEAR(logReturns(prices)[0], std::log(1.1), 1e-12);
}

TEST(QuantStatsTests, PercentileInterpolates)
{
    Vector v = {5, 1, 4, 2, 3};   // unsorted on purpose
    EXPECT_DOUBLE_EQ(percentile(v, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(percentile(v, 0.5), 3.0);
    EXPECT_DOUBLE_EQ(percentile(v, 1.0), 5.0);
    EXPECT_DOUBLE_EQ(percentile(v, 0.125), 1.5);
}

TEST(QuantStatsTests, CholeskyRebuildsTheMatrix)
{
    Matrix a = {{4, 2, 0.4}, {2, 5, 1}, {0.4, 1, 3}};
    Matrix l = cholesky(a);
    for (size_t i = 0; i < 3; i++)
        for (size_t j = 0; j < 3; j++)
        {
            double sum = 0;
            for (size_t k = 0; k < 3; k++)
                sum += l[i][k] * l[j][k];
            EXPECT_NEAR(sum, a[i][j], 1e-12);
        }
    EXPECT_DOUBLE_EQ(l[0][1], 0.0);   // lower triangular

    Matrix duplicate = {{1, 1}, {1, 1}};   // two identical stocks
    EXPECT_THROW(cholesky(duplicate), std::runtime_error);
}

TEST(QuantStatsTests, CovarianceAndCorrelation)
{
    Vector a = {1, 2, 3, 4, 5};
    Vector b = {2, 4, 6, 8, 10};
    Vector c = {5, 4, 3, 2, 1};
    Matrix cov = covarianceMatrix({a, b, c});
    EXPECT_NEAR(cov[0][0], 2.5, 1e-12);
    EXPECT_NEAR(cov[0][1], 5.0, 1e-12);
    Matrix corr = correlationFromCovariance(cov);
    EXPECT_NEAR(corr[0][1], 1.0, 1e-12);
    EXPECT_NEAR(corr[0][2], -1.0, 1e-12);
    EXPECT_NEAR(correlation(a, c), -1.0, 1e-12);
}

TEST(QuantStatsTests, RegressionRecoversTheLine)
{
    Vector x, y;
    for (int i = 0; i < 50; i++)
    {
        x.push_back(i);
        y.push_back(3.0 + 0.5 * i);
    }
    Regression r = ols(x, y);
    EXPECT_NEAR(r.alpha, 3.0, 1e-9);
    EXPECT_NEAR(r.beta, 0.5, 1e-9);
    EXPECT_NEAR(r.rSquared, 1.0, 1e-9);

    // The multiple regression agrees with the simple one
    Matrix rows;
    for (double v : x)
        rows.push_back({1.0, v});
    Vector noisy = y;
    for (size_t i = 0; i < noisy.size(); i++)
        noisy[i] += (i % 2 ? 0.3 : -0.3);
    MultiRegression m = olsMulti(rows, noisy);
    Regression s = ols(x, noisy);
    EXPECT_NEAR(m.coefficients[0], s.alpha, 1e-9);
    EXPECT_NEAR(m.coefficients[1], s.beta, 1e-9);
    EXPECT_GT(m.standardErrors[1], 0.0);
}

TEST(QuantStatsTests, AdfSeparatesMeanReversionFromARandomWalk)
{
    Vector reverting = synthetic::ar1(1000, 0.8, 1.0, 7);
    Vector walk = synthetic::ar1(1000, 1.0, 1.0, 7);   // phi = 1: random walk

    EXPECT_LT(adfStatistic(reverting), -8.0);
    EXPECT_GT(adfStatistic(walk), EG_CRITICAL_10);
}

TEST(QuantStatsTests, HalfLifeOfAnAr1)
{
    // Deviations shrink by phi a day, so they halve in ln(0.5) / ln(phi) days
    double expected = std::log(0.5) / std::log(0.9);   // ~6.6 days
    EXPECT_NEAR(halfLife(synthetic::ar1(5000, 0.9, 1.0, 11)), expected, 1.0);
    EXPECT_TRUE(std::isinf(halfLife({1, 2, 3, 4, 5, 6})));   // trending away: never reverts
}

TEST(QuantStatsTests, EngleGrangerFindsTheHedgeRatio)
{
    // log y = 0.2 + 1.5 log x + mean-reverting noise -> cointegrated with hedge ratio 1.5
    Vector x = synthetic::randomWalk(1500, 0.015, 3);
    Vector noise = synthetic::ar1(1500, 0.9, 0.01, 4);
    Vector logX, logY;
    for (size_t i = 0; i < x.size(); i++)
    {
        logX.push_back(std::log(x[i]));
        logY.push_back(0.2 + 1.5 * logX.back() + noise[i]);
    }
    CointegrationResult linked = engleGranger(logX, logY);
    EXPECT_NEAR(linked.beta, 1.5, 0.05);
    EXPECT_TRUE(linked.cointegrated(EG_CRITICAL_1));
    EXPECT_EQ(linked.verdict(), "yes (99%)");

    // Two unrelated random walks are not
    Vector other = synthetic::randomWalk(1500, 0.015, 99);
    Vector logOther;
    for (double p : other)
        logOther.push_back(std::log(p));
    CointegrationResult unrelated = engleGranger(logX, logOther);
    EXPECT_FALSE(unrelated.cointegrated());
}

TEST(QuantStatsTests, PerformanceSummary)
{
    Vector steady(252, 0.001);
    Performance p = performance(steady);
    EXPECT_NEAR(p.totalReturn, std::pow(1.001, 252) - 1.0, 1e-9);
    EXPECT_NEAR(p.cagr, p.totalReturn, 1e-9);   // exactly one year
    EXPECT_DOUBLE_EQ(p.maxDrawdown, 0.0);
    EXPECT_EQ(p.days, 252);

    // Up 10%, down 50%, up 20%: worst fall is 50% from the peak
    Performance swing = performance({0.10, -0.50, 0.20});
    EXPECT_NEAR(swing.maxDrawdown, 0.5, 1e-12);
    EXPECT_NEAR(swing.totalReturn, 1.1 * 0.5 * 1.2 - 1.0, 1e-12);
}

TEST(QuantDailyBarsTests, CsvRoundTripAndAlignment)
{
    std::string path = (std::filesystem::temp_directory_path() / "quant_daily_test.csv").string();
    std::vector<DailyBar> bars = {{"2024-01-03", 10, 11, 9, 10.5, 1000}, {"2024-01-02", 9, 10, 8, 9.5, 2000}};
    ASSERT_TRUE(writeDailyCsv(path, bars));
    std::vector<DailyBar> read = readDailyCsv(path);
    ASSERT_EQ(read.size(), 2u);
    EXPECT_EQ(read[0].date, "2024-01-02");   // sorted oldest first
    EXPECT_DOUBLE_EQ(read[1].close, 10.5);
    EXPECT_DOUBLE_EQ(read[0].volume, 2000);
    std::remove(path.c_str());
    EXPECT_TRUE(readDailyCsv(path).empty());

    // Only days both stocks traded, clipped to the range
    std::map<std::string, std::vector<DailyBar>> all = {
        {"A", {{"2024-01-02", 0, 0, 0, 1, 0}, {"2024-01-03", 0, 0, 0, 2, 0}, {"2024-01-04", 0, 0, 0, 3, 0}}},
        {"B", {{"2024-01-03", 0, 0, 0, 20, 0}, {"2024-01-04", 0, 0, 0, 30, 0}, {"2024-01-05", 0, 0, 0, 40, 0}}}};
    PriceTable table = alignCloses({"A", "B"}, all);
    ASSERT_EQ(table.days(), 2u);
    EXPECT_EQ(table.dates[0], "2024-01-03");
    EXPECT_DOUBLE_EQ(table[0][1], 3.0);
    EXPECT_DOUBLE_EQ(table[1][1], 30.0);
    EXPECT_EQ(alignCloses({"A", "B"}, all, "2024-01-04").days(), 1u);
}
