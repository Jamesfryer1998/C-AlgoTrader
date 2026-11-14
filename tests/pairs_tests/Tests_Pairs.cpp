#include <gtest/gtest.h>
#include <cmath>
#include "pairs/LeadLagBacktest.hpp"
#include "pairs/PairsBacktest.hpp"
#include "pairs/PairsSettings.hpp"
#include "pairs/PriceMatrix.hpp"

namespace
{
    const double VOLUME = 1e6;   // shares a day: $50 million or more at these prices

    // Weekdays from 2026-01-05 on (enough for these tests; holidays don't matter here)
    std::vector<std::string> weekdays(int count)
    {
        std::vector<std::string> days;
        int month = 1, day = 5, weekday = 0;   // 2026-01-05 is a Monday
        static const int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        while (static_cast<int>(days.size()) < count)
        {
            if (weekday < 5)
                days.push_back("2026-" + std::string(month < 10 ? "0" : "") + std::to_string(month) + "-"
                               + std::string(day < 10 ? "0" : "") + std::to_string(day));
            weekday = (weekday + 1) % 7;
            if (++day > monthDays[month - 1])
            {
                day = 1;
                month++;
            }
        }
        return days;
    }

    void addStock(PriceMatrix& prices, const std::string& symbol, const std::vector<double>& closes,
                  double volume = VOLUME)
    {
        std::vector<quant::DailyBar> bars;
        for (size_t i = 0; i < closes.size() && i < prices.days.size(); i++)
            bars.push_back({prices.days[i], closes[i], closes[i], closes[i], closes[i], volume});
        prices.add(symbol, bars);
    }

    PairsSettings smallSettings()
    {
        PairsSettings settings;
        settings.formationDays = 20;
        settings.tradingDays = 20;
        settings.pairs = 1;
        settings.openSigma = 2.0;
        settings.universeSize = 10;
        settings.minPrice = 5;
        settings.minDollarVolume = 1e6;
        settings.stocksEachSide = 1;
        settings.minCorrelation = 0.5;
        settings.leverage = 1.0;
        return settings;
    }

    TradingCosts noCosts()
    {
        TradingCosts costs;
        costs.slippagePercent = 0;
        costs.regulatoryFees = RegulatoryFees();
        return costs;
    }

    // A wiggle shared by stocks that move together, so their gap has a small, non-zero spread
    double wiggle(int day) { return std::sin(day * 0.9) * 0.5; }
}

TEST(PriceMatrixTests, LinesUpStocksOnTheCalendar)
{
    PriceMatrix prices;
    prices.days = weekdays(30);
    addStock(prices, "AAA", std::vector<double>(30, 10));
    std::vector<double> gappy(30, 20);
    gappy[3] = 0;   // no bar that day
    addStock(prices, "GAP", gappy);
    addStock(prices, "CHEAP", std::vector<double>(30, 2));
    addStock(prices, "THIN", std::vector<double>(30, 10), 10);

    EXPECT_EQ(prices.monthStarts(0), (std::vector<size_t>{20}));   // 2026-02-02
    EXPECT_EQ(prices.days[20], "2026-02-02");
    EXPECT_EQ(prices.indexOnOrAfter("2026-01-10"), 5u);   // the Monday after the weekend

    // AAA only: GAP missed a day, CHEAP is under $5, THIN trades too little
    EXPECT_EQ(prices.liquid(20, 20, 5, 1e6, 10), (std::vector<size_t>{0}));
    EXPECT_EQ(prices.liquid(30, 20, 5, 1e6, 10), (std::vector<size_t>{1, 0})) << "GAP's missing day is now too long ago; more dollars first";
    EXPECT_DOUBLE_EQ(prices.dailyReturn(1, 4), 0) << "no bar the day before";
}

TEST(PairsBacktestTests, PairsTheStocksThatMoveTogether)
{
    PriceMatrix prices;
    prices.days = weekdays(40);
    std::vector<double> a, b, loner;
    for (int day = 0; day < 40; day++)
    {
        a.push_back(50 + day * 0.2 + wiggle(day));
        b.push_back(50 + day * 0.2 + wiggle(day) * 0.8);
        loner.push_back(50 - day * 0.3);
    }
    addStock(prices, "A", a);
    addStock(prices, "B", b);
    addStock(prices, "LONER", loner);
    PairsBacktest pairs(smallSettings(), prices, noCosts());
    auto closest = pairs.closestPairs({0, 1, 2}, 20);
    ASSERT_EQ(closest.size(), 1u);
    EXPECT_EQ(std::min(closest[0].a, closest[0].b), 0u);
    EXPECT_EQ(std::max(closest[0].a, closest[0].b), 1u);
    EXPECT_GT(closest[0].sigma, 0);

    auto random = pairs.randomPairs({0, 1, 2}, 20);
    auto again = pairs.randomPairs({0, 1, 2}, 20);
    ASSERT_EQ(random.size(), 1u);
    EXPECT_EQ(random[0].a, again[0].a);
    EXPECT_EQ(random[0].b, again[0].b);
}

namespace
{
    // A and B track each other closely for a month (A a little noisier, so their gap has a spread), level on its
    // last day; in the next, A jumps 10% on day 23, then is level with B again from day 26
    PriceMatrix divergingPair()
    {
        PriceMatrix prices;
        prices.days = weekdays(45);
        std::vector<double> a, b;
        for (int day = 0; day < 45; day++)
        {
            double together = 50 + wiggle(day) * 0.1;
            b.push_back(together);
            if (day < 19)
                a.push_back(together + wiggle(day) * 0.02);
            else if (day >= 23 && day < 26)
                a.push_back(together * 1.1);
            else
                a.push_back(together);
        }
        addStock(prices, "A", a);
        addStock(prices, "B", b);
        return prices;
    }

    MorningBacktest::Line trade(const PriceMatrix& prices, int delayDays, TradingCosts costs = noCosts())
    {
        PairsSettings::Variant variant;
        variant.kind = PairsSettings::Kind::Pairs;
        variant.delayDays = delayDays;
        MorningBacktest::Line line;
        line.equity = 100000;
        PairsBacktest(smallSettings(), prices, costs).run(variant, true, 20, 44, line);
        return line;
    }
}

TEST(PairsBacktestTests, ShortsTheOneThatRanAheadAndClosesWhenTheyMeet)
{
    PriceMatrix prices = divergingPair();
    MorningBacktest::Line sameDay = trade(prices, 0);
    ASSERT_EQ(sameDay.stockDays.size(), 1u);
    EXPECT_EQ(sameDay.stockDays[0].day, prices.days[23]);
    EXPECT_GT(sameDay.stockDays[0].grossPnl, 0) << "short A at its peak, bought back when it fell back";
    EXPECT_EQ(sameDay.days.size(), 25u);
    EXPECT_GT(sameDay.equity, 100000);

    // A day later: opened on day 24 (still apart), closed on day 27, a day after they met
    MorningBacktest::Line dayLater = trade(prices, 1);
    ASSERT_EQ(dayLater.stockDays.size(), 1u);
    EXPECT_EQ(dayLater.stockDays[0].day, prices.days[24]);
    EXPECT_NEAR(dayLater.stockDays[0].grossPnl, sameDay.stockDays[0].grossPnl, std::fabs(sameDay.stockDays[0].grossPnl) * 0.2);
}

TEST(PairsBacktestTests, BothLegsPayCosts)
{
    TradingCosts costs = noCosts();
    costs.slippagePercent = 0.03;
    MorningBacktest::Line line = trade(divergingPair(), 0, costs);
    ASSERT_EQ(line.stockDays.size(), 1u);
    EXPECT_GT(line.stockDays[0].costs, 0);
    EXPECT_NEAR(line.stockDays[0].netPnl, line.stockDays[0].grossPnl - line.stockDays[0].costs, 1e-9);
    EXPECT_NE(line.stockDays[0].symbol.find('/'), std::string::npos);
}

TEST(LeadLagBacktestTests, FollowsTheBiggerStockThatMovesWithIt)
{
    PriceMatrix prices;
    prices.days = weekdays(30);
    std::vector<double> leader, follower, other;
    for (int day = 0; day < 30; day++)
    {
        leader.push_back(50 + wiggle(day));
        follower.push_back(50 + wiggle(day) * 0.9);
        other.push_back(50 + std::cos(day * 2.3));
    }
    // On day 21 the leader jumps 4% and the follower doesn't; it catches up the next day
    for (int day = 21; day < 30; day++)
        leader[day] *= 1.04;
    for (int day = 22; day < 30; day++)
        follower[day] *= 1.04;
    addStock(prices, "LEAD", leader, 3e6);   // most dollar volume: the bigger stock
    addStock(prices, "FOLLOW", follower, 1e6);
    addStock(prices, "OTHER", other, 2e6);

    PairsSettings settings = smallSettings();
    LeadLagBacktest leadLag(settings, prices, noCosts());
    auto universe = prices.liquid(20, 20, 5, 1e6, 10);
    ASSERT_EQ(universe, (std::vector<size_t>{0, 2, 1}));
    auto leaders = leadLag.closestLeaders(universe, 20);
    ASSERT_TRUE(leaders.count(1));
    EXPECT_EQ(leaders[1], 0u) << "FOLLOW's leader is LEAD, not OTHER";
    EXPECT_FALSE(leaders.count(0)) << "the biggest has no leader";
    auto random = leadLag.randomLeaders(universe, 20);
    EXPECT_LT(random[1], 3u);

    PairsSettings::Variant variant;
    variant.kind = PairsSettings::Kind::LeadLag;
    variant.holdDays = 1;
    MorningBacktest::Line line;
    line.equity = 100000;
    leadLag.run(variant, true, 20, 29, line);
    bool boughtFollower = false;
    for (const auto& held : line.stockDays)
        if (held.symbol == "FOLLOW" && held.day == prices.days[21])
        {
            boughtFollower = true;
            EXPECT_GT(held.grossPnl, 0) << "it caught up the next day";
        }
    EXPECT_TRUE(boughtFollower);
}

TEST(PairsSettingsTests, ReadsTheRulesAndNamesTheSteps)
{
    json config = {{"start", "2023-11-01"}, {"development_end", "2024-10-31"}, {"validation_end", "2025-10-31"},
                   {"end", ""}, {"data_start", "2022-09-01"}, {"formation_days", 252},
                   {"pairs", {{"pairs", 20}, {"trading_days", 126}, {"open_sigma", 2.0}}},
                   {"lead_lag", {{"stocks_each_side", 20}, {"min_correlation", 0.5}}},
                   {"variants", {{{"name", "Pairs, a day later"}, {"kind", "pairs"}, {"delay_days", 1}},
                                 {{"name", "Lead-lag, 5 days"}, {"kind", "lead_lag"}, {"hold_days", 5}}}},
                   {"gates", {{"min_sharpe", 1.0}}}};
    PairsSettings settings = PairsSettings::fromConfig(config, "2026-11-13");
    EXPECT_EQ(settings.end, "2026-11-13");
    ASSERT_EQ(settings.variants.size(), 2u);
    EXPECT_EQ(settings.variants[1].kind, PairsSettings::Kind::LeadLag);
    EXPECT_EQ(settings.variants[1].holdDays, 5);
    MorningSettings report = settings.forReport();
    EXPECT_EQ(report.variants[0].step, PairsSettings::PAIRS_STEP);
    EXPECT_EQ(report.variants[1].step, PairsSettings::LEAD_LAG_STEP);
    EXPECT_EQ(report.stocks, 20);
    EXPECT_EQ(report.periodOf("2025-01-02"), MorningSettings::VALIDATION);
}
