#include <gtest/gtest.h>
#include <cmath>
#include <numeric>
#include "timing/TimingBacktest.hpp"
#include "timing/TimingRule.hpp"
#include "timing/TimingSeries.hpp"
#include "timing/TimingSettings.hpp"

namespace
{
    const double PRICE = 100;

    // Weekday dates from 2017-01-02, any month length right enough for these tests
    std::vector<std::string> dates(size_t count)
    {
        std::vector<std::string> days;
        int year = 2017, month = 1, day = 2, weekday = 1;
        const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        while (days.size() < count)
        {
            if (weekday >= 1 && weekday <= 5)
            {
                char text[40];
                std::snprintf(text, sizeof(text), "%04d-%02d-%02d", year, month, day);
                days.push_back(text);
            }
            weekday = (weekday + 1) % 7;
            if (++day > lengths[month - 1])
            {
                day = 1;
                if (++month > 12)
                {
                    month = 1;
                    year++;
                }
            }
        }
        return days;
    }

    // Bars that open at the close before
    std::vector<quant::DailyBar> barsOf(const std::vector<double>& closes)
    {
        std::vector<std::string> days = dates(closes.size());
        std::vector<quant::DailyBar> bars;
        for (size_t i = 0; i < closes.size(); i++)
        {
            double open = i == 0 ? closes[0] : closes[i - 1];
            bars.push_back({days[i], open, std::max(open, closes[i]), std::min(open, closes[i]), closes[i], 1e6});
        }
        return bars;
    }

    TimingSettings settingsFor(double slippage = 0)
    {
        TimingSettings settings;
        settings.start = "2017-01-01";
        settings.developmentEnd = "2030-01-01";
        settings.validationEnd = "2031-01-01";
        settings.end = "2032-01-01";
        settings.cashSymbol = "";
        settings.slippagePercent = slippage;
        return settings;
    }

    TimingRule rule(const std::string& family, const json& parameters)
    {
        TimingRule made;
        made.family = family;
        made.parameters = parameters;
        return made;
    }
}

TEST(TimingRuleTests, EveryPossibleCombinationOfEachFamily)
{
    json families = {{"trend", {{"average", {"sma", "ema"}}, {"days", {50, 200}}, {"band_percent", {0, 1, 2}}}},
                     {"cross", {{"average", {"sma"}}, {"fast", {10, 50}}, {"slow", {50, 200}}}},
                     {"breakout", {{"enter_days", {20, 50}}, {"exit_days", {10, 20}}}},
                     {"no_such_family", {{"days", {1, 2}}}}};
    std::vector<TimingRule> rules = TimingRule::expand(families);
    // trend 2 x 2 x 3 = 12; cross: 10/50, 10/200, 50/200 (not 50/50) = 3; breakout: 20/10, 50/10, 50/20 = 3
    EXPECT_EQ(rules.size(), 18u);
    EXPECT_EQ(rules[0].family, "trend");
    EXPECT_EQ(rule("trend", {{"average", "sma"}, {"days", 200}}).name(), "trend: average sma, days 200");
}

TEST(TimingRuleTests, TrendIsInAboveItsAverageWithABand)
{
    std::vector<double> closes;
    for (int i = 0; i < 30; i++)
        closes.push_back(PRICE + i);          // rising
    for (int i = 0; i < 30; i++)
        closes.push_back(PRICE + 30 - 2 * i); // falling
    TimingSeries series(barsOf(closes), {});
    auto targets = rule("trend", {{"average", "sma"}, {"days", 10}, {"band_percent", 0}}).targets(series);
    EXPECT_EQ(targets[5], 0);    // no average yet
    EXPECT_EQ(targets[20], 1);
    EXPECT_EQ(targets[50], 0);
    // A wide band holds the last call through the turn
    auto banded = rule("trend", {{"average", "sma"}, {"days", 10}, {"band_percent", 50}}).targets(series);
    EXPECT_EQ(banded[20], 0);    // never 50% over its average
}

TEST(TimingRuleTests, DipsAreHeldNoLongerThanTheirLimit)
{
    std::vector<double> closes(40, PRICE);
    for (int i = 20; i < 40; i++)
        closes[i] = PRICE - (i - 19);   // falls every day from day 20: RSI stays near 0
    TimingSeries series(barsOf(closes), {});
    auto targets = rule("dip", {{"rsi_days", 2}, {"enter_below", 10}, {"exit_above", 50}, {"max_hold_days", 3},
                                {"trend_days", 0}}).targets(series);
    EXPECT_EQ(targets[19], 0);   // before the fall
    int held = 0, longest = 0;
    for (double target : targets)
    {
        held = target > 0 ? held + 1 : 0;
        longest = std::max(longest, held);
    }
    EXPECT_GT(longest, 0);
    EXPECT_LE(longest, 3);
}

TEST(TimingRuleTests, TurnOfTheMonthKnowsTheTradingDaysAroundEachMonthEnd)
{
    TimingSeries series(barsOf(std::vector<double>(60, PRICE)), {});
    auto targets = rule("turn_of_month", {{"last_days", 1}, {"first_days", 1}}).targets(series);
    // Decided the day before: in for January's last trading day and February's first
    for (size_t d = 0; d + 1 < series.size(); d++)
    {
        bool lastOfJanuary = series.days[d + 1] == "2017-01-31", firstOfFebruary = series.days[d + 1] == "2017-02-01";
        EXPECT_EQ(targets[d] > 0, lastOfJanuary || firstOfFebruary || series.daysToMonthEnd(d + 1) == 1
                                  || series.dayOfMonth(d + 1) == 1) << series.days[d + 1];
    }
    EXPECT_EQ(series.dayOfMonth(0), 1);
}

TEST(TimingBacktestTests, AllInIsBuyAndHoldLessOneEntry)
{
    std::vector<double> closes;
    for (int i = 0; i < 50; i++)
        closes.push_back(PRICE * (1 + 0.01 * std::sin(i)));
    TimingSeries series(barsOf(closes), {});
    TimingSettings settings = settingsFor(0.03);
    TimingBacktest backtest(settings, series);
    std::vector<double> allIn(series.size(), 1.0);
    auto traded = backtest.trade(allIn, 1, series.size() - 1);
    auto held = backtest.buyAndHold(1, series.size() - 1);
    ASSERT_EQ(traded.returns.size(), held.returns.size());
    // Day 1: flat overnight (it starts out), in for the session, paying one entry
    EXPECT_NEAR(traded.returns[0], series.close[1] / series.open[1] - 1 - 0.0003, 1e-12);
    for (size_t i = 1; i < traded.returns.size(); i++)
        EXPECT_NEAR(traded.returns[i], held.returns[i], 1e-12);
    EXPECT_EQ(std::accumulate(traded.changes.begin(), traded.changes.end(), 0), 1);
}

TEST(TimingBacktestTests, HalfInWithoutTimingAddsNothingOverTheMix)
{
    std::vector<double> closes;
    for (int i = 0; i < 300; i++)
        closes.push_back(PRICE * std::exp(0.001 * i + 0.02 * std::sin(i * 0.7)));
    TimingSeries series(barsOf(closes), {});
    TimingSettings settings = settingsFor(0);
    TimingBacktest backtest(settings, series);
    auto traded = backtest.trade(std::vector<double>(series.size(), 0.5), 1, series.size() - 1);
    auto measures = backtest.measure(traded, MorningSettings::DEVELOPMENT);
    EXPECT_NEAR(measures.exposure, 0.5, 1e-12);
    EXPECT_NEAR(measures.edge, 0, 0.002);   // only the open-to-open timing of day 1 differs
    EXPECT_GT(measures.capture, 0.3);
    EXPECT_LT(measures.capture, 0.7);
}

TEST(TimingBacktestTests, RandomTimingKeepsTheExposureAndTheStretches)
{
    TimingSeries series(barsOf(std::vector<double>(100, PRICE)), {});
    TimingSettings settings = settingsFor(0);
    TimingBacktest backtest(settings, series);
    std::vector<double> targets(100, 0);
    for (int i = 10; i < 20; i++)
        targets[i] = 1;
    for (int i = 50; i < 53; i++)
        targets[i] = 1;
    std::mt19937_64 random(1);
    auto shuffled = backtest.shuffled(targets, 1, 99, random);
    EXPECT_EQ(std::accumulate(shuffled.begin(), shuffled.end(), 0.0), 13.0);
    int stretchesIn = 0;
    for (size_t i = 0; i < shuffled.size(); i++)
        stretchesIn += shuffled[i] > 0 && (i == 0 || shuffled[i - 1] == 0) ? 1 : 0;
    EXPECT_LE(stretchesIn, 2);   // the two stretches in (they may now touch)
    EXPECT_GE(stretchesIn, 1);
}

TEST(TimingBacktestTests, SpearmanRanks)
{
    EXPECT_NEAR(TimingBacktest::spearman({1, 2, 3, 4}, {10, 20, 30, 40}), 1, 1e-12);
    EXPECT_NEAR(TimingBacktest::spearman({1, 2, 3, 4}, {4, 3, 2, 1}), -1, 1e-12);
    EXPECT_NEAR(TimingBacktest::spearman({1, 2, 3}, {5, 5, 5}), 0, 1e-12);
}
