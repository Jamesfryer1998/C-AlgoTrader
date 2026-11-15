#include <gtest/gtest.h>
#include <cmath>
#include "grid/GridBacktest.hpp"
#include "grid/GridSettings.hpp"
#include "pairs/PriceMatrix.hpp"

namespace
{
    const double CAPITAL = 100000;

    GridSettings settingsFor()
    {
        GridSettings settings;
        settings.startInvested = 0.8;
        settings.slippagePercent = 0;
        settings.startingCapital = CAPITAL;
        settings.formationDays = 2;
        return settings;
    }

    GridSettings::Variant rule(bool mirror = false, bool atClose = true)
    {
        GridSettings::Variant variant;
        variant.name = "test";
        variant.triggerPercent = 5;
        variant.stepPercent = 10;
        variant.mirror = mirror;
        variant.atClose = atClose;
        return variant;
    }

    // One stock whose opens are the close before
    PriceMatrix oneStock(const std::vector<double>& closes)
    {
        PriceMatrix prices;
        std::vector<quant::DailyBar> bars;
        for (size_t i = 0; i < closes.size(); i++)
        {
            prices.days.push_back("2026-01-" + std::string(i < 9 ? "0" : "") + std::to_string(i + 1));
            double open = i == 0 ? closes[0] : closes[i - 1];
            bars.push_back({prices.days[i], open, open, open, closes[i], 1e6});
        }
        prices.add("A", bars);
        return prices;
    }
}

TEST(GridBacktestTests, HeldIsTheStartingMixLeftAlone)
{
    GridSettings settings = settingsFor();
    PriceMatrix prices = oneStock({100, 110, 90, 100});
    GridBacktest backtest(settings, prices);
    auto held = backtest.run(rule(), {0}, 1, 3, GridBacktest::Mode::Held, "held");
    // 80% in the stock: it ends where it began, so the account does too
    EXPECT_NEAR(held.equity, CAPITAL, 1e-6);
    EXPECT_NEAR(held.returns[0], 0.8 * 0.10, 1e-12);
}

TEST(GridBacktestTests, BuysAfterADropAndSellsAfterAPop)
{
    GridSettings settings = settingsFor();
    // Down 10% (buy 10% more at 90), then back up 11.1% (sell 10% at 100)
    PriceMatrix prices = oneStock({100, 90, 100});
    GridBacktest backtest(settings, prices);
    auto line = backtest.run(rule(), {0}, 1, 2, GridBacktest::Mode::Rule, "rule");
    double shares = 800, cash = 20000;
    shares *= 1.1;                       // bought 80 at 90
    cash -= 80 * 90;
    cash += 0.1 * shares * 100;          // sold 88 at 100
    shares *= 0.9;
    EXPECT_NEAR(line.equity, cash + shares * 100, 1e-6);
    EXPECT_GT(line.equity, CAPITAL);     // a round trip that bought low and sold high
    auto mirror = backtest.run(rule(true), {0}, 1, 2, GridBacktest::Mode::Rule, "rule");
    EXPECT_LT(mirror.equity, CAPITAL);
}

TEST(GridBacktestTests, BuysOnlyAsFarAsTheCashGoes)
{
    GridSettings settings = settingsFor();
    settings.startInvested = 1.0;   // no cash at all
    PriceMatrix prices = oneStock({100, 90, 80});
    GridBacktest backtest(settings, prices);
    auto line = backtest.run(rule(), {0}, 1, 2, GridBacktest::Mode::Rule, "rule");
    for (const auto& day : line.stockDays)
        EXPECT_EQ(day.trades, 0);
    EXPECT_NEAR(line.equity, CAPITAL * 0.8, 1e-6);
}

TEST(GridBacktestTests, FillsAtTheNextOpenUnlessAtTheClose)
{
    GridSettings settings = settingsFor();
    PriceMatrix prices = oneStock({100, 90, 92, 92});
    GridBacktest backtest(settings, prices);
    auto line = backtest.run(rule(false, false), {0}, 1, 3, GridBacktest::Mode::Rule, "rule");
    // Signal at day 1's close (90); bought at day 2's open (90, the close before); one fill in all
    int fills = 0;
    for (const auto& day : line.stockDays)
        fills += day.trades;
    EXPECT_EQ(fills, 1);
    EXPECT_EQ(line.stockDays[1].trades, 1);
}

TEST(GridBacktestTests, PicksTheMostTradedStocksThatArentFunds)
{
    GridSettings settings = settingsFor();
    settings.funds = {"SPY"};
    PriceMatrix prices;
    prices.days = {"2026-01-01", "2026-01-02", "2026-01-03"};
    auto add = [&](const std::string& symbol, double volume) {
        std::vector<quant::DailyBar> bars;
        for (const auto& day : prices.days)
            bars.push_back({day, 50, 50, 50, 50, volume});
        prices.add(symbol, bars);
    };
    add("SPY", 1e9);
    add("BIG", 1e7);
    add("SMALL", 1e5);
    GridBacktest backtest(settings, prices);
    auto rows = backtest.mostTraded(2, 1);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(prices.symbols[rows[0]], "BIG");
    EXPECT_EQ(backtest.fundRows().size(), 1u);
}
