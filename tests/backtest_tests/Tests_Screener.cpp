#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include "../../src/backtest/StrategyTester.hpp"
#include "../../src/backtest/Screener.hpp"
#include "../../src/util/Log.hpp"

namespace
{
    // Weekday daily bars from 2019-01-01: a price that swings around 100 with some noise, so RSI and
    // MEANREV find trades
    std::vector<quant::DailyBar> dailyBars(int days, unsigned seed = 7)
    {
        std::vector<quant::DailyBar> bars;
        std::time_t t = 1546300800;   // 2019-01-01 UTC
        double price = 100;
        int i = 0;
        while (static_cast<int>(bars.size()) < days)
        {
            std::tm day = *std::gmtime(&t);
            t += 24 * 60 * 60;
            if (day.tm_wday == 0 || day.tm_wday == 6)
                continue;
            seed = seed * 1103515245u + 12345u;
            double noise = (((seed >> 16) % 1000) / 1000.0 - 0.5) * 0.03;
            double previous = price;
            price = 100.0 * (1.0 + 0.15 * std::sin(i++ / 15.0)) * (1.0 + noise);
            char date[16];
            std::strftime(date, sizeof(date), "%Y-%m-%d", &day);
            bars.push_back({date, previous, std::max(previous, price), std::min(previous, price), price, 1e6});
        }
        return bars;
    }

    json baseConfig()
    {
        return {{"ticker", "TEST"}, {"max_position_size", 10}, {"max_exposure", 5.0}, {"slippage_tolerance", 50.0},
                {"strategies", {{{"name", "RSI"}, {"active", 1}, {"period", 14}, {"overbought_threshold", 70.0},
                                 {"oversold_threshold", 30.0}, {"stop_loss", 10}, {"take_profit", 10}},
                                {{"name", "MEANREV"}, {"active", 0}, {"lookback_period", 20}, {"moving_average_period", 20},
                                 {"entry_threshold", 1.5}, {"exit_threshold", 0.0}, {"stop_loss", 10}, {"take_profit", 10}}}}};
    }

    json screenSettings()
    {
        return {{"test_start", "2022-01-01"}, {"test_warmup_bars", 60}, {"min_train_days", 300}, {"min_test_days", 60},
                {"starting_capital", 100000.0}, {"commission_per_trade", 0.0}, {"slippage_percent", 0.01},
                {"objective", "sharpe"}, {"min_closed_trades", 3}, {"max_combinations", 0},
                {"strategies", {
                    {"RSI", {{"period", {7, 14}}, {"oversold_threshold", {25, 30}}, {"overbought_threshold", {70, 75}}}},
                    {"MEANREV", {{"lookback_period", {20, 40}}, {"entry_threshold", {1.5, 2.0}}}}
                }}};
    }

    struct Quiet
    {
        Quiet() { Log::verbose = false; }
        ~Quiet() { Log::verbose = true; }
    };
}

TEST(ScreenerTests, DailyBarsBecomeBacktestBars)
{
    std::vector<MarketCondition> bars = Screener::toBars("KO", {{"2024-01-02", 60.0, 61.0, 59.0, 60.5, 5e9}});
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_EQ(bars[0].DateTime, "2024-01-02 16:00:00");
    EXPECT_EQ(bars[0].Ticker, "KO");
    EXPECT_FLOAT_EQ(bars[0].Close, 60.5f);
    EXPECT_GT(bars[0].Volume, 0);   // 5 billion clamped to fit, not overflowed negative
}

TEST(ScreenerTests, WarmupBarsAreHistoryOnly)
{
    Quiet quiet;
    std::vector<MarketCondition> bars = Screener::toBars("TEST", dailyBars(400));
    json config = baseConfig();
    config["bars_per_year"] = 252.0;

    StrategyTester cold(config);
    cold.keepEquityCurve(false);
    cold.runOn(std::vector<MarketCondition>(bars.begin() + 300, bars.end()));

    // The same last 100 days, with 300 earlier days fed in first as history: no trades happen during them
    StrategyTester warm(config);
    warm.keepEquityCurve(false);
    warm.setWarmupBars(300);
    warm.runOn(bars);

    StrategyTester everything(config);
    everything.keepEquityCurve(false);
    everything.runOn(bars);

    EXPECT_LT(warm.getSummary().fills, everything.getSummary().fills);
    // RSI(14) needs 14 bars: the cold run loses its first 14 days, the warm one can trade from day one
    EXPECT_GE(warm.getSummary().fills, cold.getSummary().fills);
}

TEST(ScreenerTests, SharpeIsAnnualisedForDailyBars)
{
    Quiet quiet;
    std::vector<MarketCondition> bars = Screener::toBars("TEST", dailyBars(400));
    json minute = baseConfig();
    json daily = baseConfig();
    daily["bars_per_year"] = 252.0;

    StrategyTester a(minute), b(daily);
    a.keepEquityCurve(false);
    b.keepEquityCurve(false);
    a.runOn(bars);
    b.runOn(bars);
    ASSERT_NE(a.getSummary().sharpeRatio, 0.0);
    // Same trades, so the only difference is the annualising factor: sqrt(252) instead of sqrt(252 x 390)
    // (within 1%: the simulated broker's slippage is random)
    double expected = std::sqrt(1.0 / 390.0);
    EXPECT_NEAR(b.getSummary().sharpeRatio / a.getSummary().sharpeRatio, expected, expected * 0.01);
}

TEST(ScreenerTests, ScreensEachStrategyAndComparesWithBuyAndHold)
{
    Quiet quiet;
    Screener screener(baseConfig(), screenSettings());
    std::vector<quant::DailyBar> daily = dailyBars(1100);   // 2019 to mid 2023
    std::vector<Screener::Row> rows = screener.screenStock("TEST", daily, {"RSI", "MEANREV"}, 2);

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].strategy, "RSI");
    EXPECT_EQ(rows[1].strategy, "MEANREV");
    EXPECT_EQ(rows[0].combinations, 8);
    EXPECT_EQ(rows[1].combinations, 4);
    EXPECT_EQ(rows[0].testStart, "2022-01-03");   // first weekday of 2022
    EXPECT_EQ(rows[0].trainDays + rows[0].testDays, 1100);

    // Buy and hold over the test days, from the first to the last test close
    double first = 0, last = daily.back().close;
    for (const auto& d : daily)
        if (d.date >= "2022-01-01") { first = d.close; break; }
    EXPECT_NEAR(rows[0].buyHoldReturn, last / first - 1.0, 1e-6);   // backtest bars hold float prices

    for (const auto& row : rows)
    {
        ASSERT_TRUE(row.found) << row.strategy;
        EXPECT_GE(row.train.closedTrades, 3);
        EXPECT_GT(row.test.fills, 0);
        EXPECT_FALSE(row.parameters.empty());
    }
}

TEST(ScreenerTests, TestYearsNeverChangeThePick)
{
    Quiet quiet;
    Screener screener(baseConfig(), screenSettings());
    std::vector<quant::DailyBar> daily = dailyBars(1100);
    std::vector<Screener::Row> before = screener.screenStock("TEST", daily, {"RSI"}, 2);

    // Wreck the test years (a steady slide): the pick and its training result must stay exactly the same
    int k = 0;
    for (auto& d : daily)
        if (d.date >= "2022-01-01")
            d.close *= 1.0 - 0.002 * k++;
    std::vector<Screener::Row> after = screener.screenStock("TEST", daily, {"RSI"}, 2);

    EXPECT_EQ(before[0].parameters, after[0].parameters);
    EXPECT_DOUBLE_EQ(before[0].train.sharpeRatio, after[0].train.sharpeRatio);
    EXPECT_NE(before[0].buyHoldReturn, after[0].buyHoldReturn);
}

TEST(ScreenerTests, TooLittleHistoryFindsNothing)
{
    Quiet quiet;
    Screener screener(baseConfig(), screenSettings());
    std::vector<Screener::Row> rows = screener.screenStock("NEW", dailyBars(200), {"RSI"}, 1);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_FALSE(rows[0].found);
}

TEST(ScreenerTests, SummaryPicksAndCsv)
{
    auto row = [](const std::string& symbol, const std::string& strategy, double train, double test, double buyHold) {
        Screener::Row r;
        r.symbol = symbol;
        r.strategy = strategy;
        r.found = true;
        r.train.sharpeRatio = train;
        r.test.sharpeRatio = test;
        r.test.closedTrades = 5;
        r.buyHoldSharpe = buyHold;
        r.parameters = {{"period", 14}, {"signal_type", "crossover"}};
        return r;
    };
    std::vector<Screener::Row> rows = {row("A", "RSI", 2.0, -0.5, 0.5), row("B", "RSI", 1.0, 0.8, 0.5),
                                       row("C", "RSI", 0.2, 0.3, 0.9), row("A", "MACD", 0.6, 0.6, 0.5)};
    Screener::Row nothing;
    nothing.symbol = "D";
    nothing.strategy = "RSI";
    rows.push_back(nothing);

    auto summary = Screener::summarise(rows, {"RSI", "MACD"});
    ASSERT_EQ(summary.size(), 2u);
    EXPECT_EQ(summary[0].stocks, 4);
    EXPECT_EQ(summary[0].found, 3);
    EXPECT_EQ(summary[0].positiveTest, 2);
    EXPECT_EQ(summary[0].beatBuyHold, 1);   // only B
    EXPECT_DOUBLE_EQ(summary[0].medianTestSharpe, 0.3);
    EXPECT_LT(summary[0].trainTestCorrelation, 0.0);   // best in training was worst in test

    auto top = Screener::bestOnTraining(rows, 2);
    ASSERT_EQ(top.size(), 2u);
    EXPECT_EQ(top[0].symbol, "A");
    EXPECT_EQ(top[1].symbol, "B");

    auto held = Screener::goodInBothPeriods(rows);
    ASSERT_EQ(held.size(), 2u);
    EXPECT_EQ(held[0].symbol, "B");   // best test Sharpe first
    EXPECT_EQ(held[1].strategy, "MACD");

    EXPECT_EQ(Screener::describe(rows[0].parameters), "period 14 crossover");

    std::string path = (std::filesystem::temp_directory_path() / "screen_test.csv").string();
    ASSERT_TRUE(Screener::writeCsv(path, rows));
    std::vector<Screener::Row> read = Screener::readCsv(path);
    ASSERT_EQ(read.size(), rows.size());
    EXPECT_EQ(read[1].symbol, "B");
    EXPECT_DOUBLE_EQ(read[1].test.sharpeRatio, 0.8);
    EXPECT_EQ(read[1].parameters, rows[1].parameters);   // JSON with commas and quotes survives the CSV
    EXPECT_FALSE(read[4].found);
    std::remove(path.c_str());
}

TEST(ScreenerTests, TopPicksNeedBothPeriodsAndOnePerStock)
{
    auto row = [](const std::string& symbol, const std::string& strategy, double train, double test, int trades) {
        Screener::Row r;
        r.symbol = symbol;
        r.strategy = strategy;
        r.found = true;
        r.train.sharpeRatio = train;
        r.test.sharpeRatio = test;
        r.test.closedTrades = trades;
        r.buyHoldSharpe = 0.4;
        return r;
    };
    std::vector<Screener::Row> rows = {
        row("A", "RSI", 3.0, 0.6, 10),       // great training, only just OK test: weaker side 0.6
        row("B", "MACD", 1.0, 1.1, 10),      // good in both: weaker side 1.0 -> first
        row("B", "RSI", 0.9, 0.9, 10),       // same stock as a better pick: skipped
        row("C", "MEANREV", 0.8, 2.0, 10),   // weaker side 0.8
        row("D", "RSI", 2.0, 2.0, 2),        // too few test trades
        row("E", "RSI", 2.0, 0.3, 10)};      // test too weak

    std::vector<Screener::Row> picks = Screener::topPicks(rows, 5);
    ASSERT_EQ(picks.size(), 3u);
    EXPECT_EQ(picks[0].symbol, "B");
    EXPECT_EQ(picks[0].strategy, "MACD");
    EXPECT_EQ(picks[1].symbol, "C");
    EXPECT_EQ(picks[2].symbol, "A");
    EXPECT_EQ(Screener::topPicks(rows, 1).size(), 1u);
}

TEST(ScreenerTests, PickValuesTrackEachPicksProfit)
{
    Quiet quiet;
    Screener screener(baseConfig(), screenSettings());
    std::vector<quant::DailyBar> daily = dailyBars(700);

    Screener::Row rsi;
    rsi.symbol = "TEST";
    rsi.strategy = "RSI";
    rsi.found = true;
    rsi.parameters = {{"period", 7}, {"oversold_threshold", 30}, {"overbought_threshold", 70}};
    Screener::Row meanrev = rsi;
    meanrev.strategy = "MEANREV";
    meanrev.parameters = {{"lookback_period", 20}, {"entry_threshold", 1.5}};

    quant::PriceTable values = screener.dailyValues({rsi, meanrev}, {{"TEST", daily}});
    ASSERT_EQ(values.symbols.size(), 2u);
    EXPECT_EQ(values.symbols[0], "TEST RSI");
    EXPECT_EQ(values.days(), 700u);
    EXPECT_EQ(values.dates.front(), daily.front().date);

    // Starts at the full-position capital (10 shares at the first price) and moves with the strategy's P&L
    double capital = 10 * daily.front().close;
    EXPECT_NEAR(values[0][0], capital, capital * 0.05);
    EXPECT_NE(values[0].back(), values[0].front());
    EXPECT_NE(values[0], values[1]);

    // Unknown stock: left out
    Screener::Row missing = rsi;
    missing.symbol = "NONE";
    EXPECT_EQ(screener.dailyValues({missing}, {{"TEST", daily}}).symbols.size(), 0u);
}

namespace
{
    // Regular-hours minute bars (09:30-15:59) for `days` weekdays from 2025-01-06, swinging over hours
    std::vector<MarketCondition> minuteBars(int days, const std::string& ticker = "MIN")
    {
        std::vector<MarketCondition> bars;
        std::time_t t = 1736121600;   // 2025-01-06 UTC (a Monday)
        unsigned seed = 3;
        double price = 100;
        int made = 0, k = 0;
        while (made < days)
        {
            std::tm day = *std::gmtime(&t);
            t += 24 * 60 * 60;
            if (day.tm_wday == 0 || day.tm_wday == 6)
                continue;
            char date[16];
            std::strftime(date, sizeof(date), "%Y-%m-%d", &day);
            for (int m = 0; m < 390; m++, k++)
            {
                seed = seed * 1103515245u + 12345u;
                double noise = (((seed >> 16) % 1000) / 1000.0 - 0.5) * 0.004;
                double previous = price;
                price = 100.0 * (1.0 + 0.03 * std::sin(k / 120.0)) * (1.0 + noise);
                char time[32];
                std::snprintf(time, sizeof(time), "%s %02d:%02d:00", date, 9 + (30 + m) / 60, (30 + m) % 60);
                bars.emplace_back(time, ticker, static_cast<float>(previous), static_cast<float>(price), 1000, "1m");
            }
            made++;
        }
        return bars;
    }

    json minuteSettings()
    {
        json s = screenSettings();
        s["bars_per_year"] = 252.0 * 390.0;
        s["test_start"] = "2025-04-01";
        s["min_train_days"] = 40;
        s["min_test_days"] = 15;
        s["test_warmup_bars"] = 100;
        return s;
    }
}

TEST(ScreenerTests, ScreensMinuteBars)
{
    Quiet quiet;
    Screener screener(baseConfig(), minuteSettings());
    std::vector<MarketCondition> bars = minuteBars(80);   // ~ 2025-01-06 to 2025-04-25
    EXPECT_EQ(Screener::countDays(bars), 80);

    std::vector<Screener::Row> rows = screener.screenStock("MIN", bars, {"RSI"}, 2);
    ASSERT_EQ(rows.size(), 1u);
    const auto& row = rows[0];
    EXPECT_EQ(row.trainDays + row.testDays, 80);   // days, not bars
    EXPECT_EQ(row.testStart, "2025-04-01");
    ASSERT_TRUE(row.found);
    EXPECT_GT(row.test.fills, 0);

    // Buy and hold is measured bar by bar and annualised for minute bars, like the strategies
    quant::Vector closes;
    for (const auto& bar : bars)
        if (bar.DateTime >= "2025-04-01")
            closes.push_back(bar.Close);
    quant::Performance expected = quant::performance(quant::simpleReturns(closes), 252.0 * 390.0);
    EXPECT_NEAR(row.buyHoldSharpe, expected.sharpe, 1e-9);
    EXPECT_NEAR(row.buyHoldReturn, closes.back() / closes.front() - 1.0, 1e-9);
}

TEST(ScreenerTests, MinutePickValuesAreOnePerDay)
{
    Quiet quiet;
    Screener screener(baseConfig(), minuteSettings());
    Screener::Row pick;
    pick.symbol = "MIN";
    pick.strategy = "RSI";
    pick.found = true;
    pick.parameters = {{"period", 14}, {"oversold_threshold", 30}, {"overbought_threshold", 70}};

    std::vector<MarketCondition> bars = minuteBars(30);
    quant::PriceTable values = screener.dailyValues({pick}, std::map<std::string, std::vector<MarketCondition>>{{"MIN", bars}});
    ASSERT_EQ(values.symbols.size(), 1u);
    EXPECT_EQ(values.days(), 30u);   // the last value of each day
    EXPECT_EQ(values.dates.front(), "2025-01-06");

    json ready = screener.strategySettings(pick);   // full block for strategy.json
    EXPECT_EQ(ready["name"], "RSI");
    EXPECT_EQ(ready["period"], 14);
    EXPECT_EQ(ready["stop_loss"], 10);   // not searched: kept from strategy.json
    EXPECT_EQ(ready["active"], 1);
}
