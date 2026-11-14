#include <gtest/gtest.h>
#include "MorningTestBars.hpp"
#include "morning/DriftBacktest.hpp"

using namespace MorningTestBars;

namespace
{
    const std::vector<std::string> DAYS = {"2026-03-02", "2026-03-03", "2026-03-04", "2026-03-05", "2026-03-06"};

    TradingCosts noCosts()
    {
        TradingCosts costs;
        costs.slippagePercent = 0;
        costs.regulatoryFees = RegulatoryFees();
        return costs;
    }

    // UP closes 100, 102, 104, ...; its first day opened at 98 (an up day)
    DriftBacktest::DailyBySymbol rising()
    {
        DriftBacktest::DailyBySymbol daily;
        for (size_t i = 0; i < DAYS.size(); i++)
        {
            double close = 100 + 2.0 * i;
            daily["UP"][DAYS[i]] = {DAYS[i], i == 0 ? 98 : close, close + 1, close - 1, close, 1e6};
        }
        return daily;
    }

    MorningSettings driftSettings()
    {
        MorningSettings settings = simpleSettings();   // 2 stocks a day, $10,000
        settings.driftLeverage = 1.0;
        return settings;
    }

    MorningSettings::Variant variant(int days, bool fade = false, bool dayMove = false)
    {
        MorningSettings::Variant drift;
        drift.name = "drift";
        drift.holdDays = days;
        drift.fade = fade;
        drift.dayMove = dayMove;
        return drift;
    }

    MorningBacktest::Line run(const MorningSettings::Variant& drift, int direction, TradingCosts costs = noCosts())
    {
        DriftBacktest::PicksByDay picks = {{DAYS[0], {{"UP", 1.0, 3.0, direction}}}};
        MorningBacktest::Line line;
        line.equity = 10000;
        DriftBacktest(driftSettings(), costs).run(drift, picks, rising(), DAYS, line);
        return line;
    }
}

TEST(DriftBacktestTests, BuysAtTheCloseAndSellsDaysLater)
{
    MorningBacktest::Line line = run(variant(2), 1);
    // $10,000 / (2 days x 2 stocks) = $2,500 at 100: 25 shares, sold two closes later at 104
    ASSERT_EQ(line.stockDays.size(), 1u);
    EXPECT_EQ(line.stockDays[0].day, DAYS[0]);
    EXPECT_DOUBLE_EQ(line.stockDays[0].grossPnl, 25 * 4);
    EXPECT_DOUBLE_EQ(line.equity, 10000 + 100);
    ASSERT_EQ(line.returns.size(), DAYS.size());
    EXPECT_DOUBLE_EQ(line.returns[0], 0);
    EXPECT_DOUBLE_EQ(line.returns[1], 50.0 / 10000) << "marked to each close while held";
    EXPECT_DOUBLE_EQ(line.returns[3], 0) << "sold";
}

TEST(DriftBacktestTests, FadeShortsTheMoveAndTheDaysMoveCanSetTheDirection)
{
    MorningBacktest::Line fade = run(variant(1, true), 1);
    ASSERT_EQ(fade.stockDays.size(), 1u);
    EXPECT_DOUBLE_EQ(fade.stockDays[0].grossPnl, -50 * 2) << "shorted 50 at 100, bought back at 102";
    EXPECT_EQ(fade.stockDays[0].wins, 0);

    // The opening bar fell, but the day rose (98 -> 100): "the day's move" buys
    MorningBacktest::Line byDay = run(variant(1, false, true), -1);
    EXPECT_DOUBLE_EQ(byDay.stockDays[0].grossPnl, 50 * 2);
}

TEST(DriftBacktestTests, PaysSlippageBothWaysAndFeesOnTheSale)
{
    TradingCosts costs = noCosts();
    costs.slippagePercent = 0.03;
    costs.regulatoryFees = RegulatoryFees(json::object());
    MorningBacktest::Line line = run(variant(1), 1, costs);
    const StockDay& trade = line.stockDays[0];
    double expected = 50 * 100 * 0.0003 + 50 * 102 * 0.0003 + costs.regulatoryFees.onSale(50, 102);
    EXPECT_NEAR(trade.costs, expected, 1e-9);
    EXPECT_NEAR(trade.netPnl, 100 - expected, 1e-9);
    EXPECT_NEAR(line.equity, 10000 + trade.netPnl, 1e-9);
}
