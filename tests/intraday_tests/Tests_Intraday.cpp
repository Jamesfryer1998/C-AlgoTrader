#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include "intraday/IntradayBacktest.hpp"
#include "intraday/IntradayDay.hpp"
#include "intraday/IntradaySettings.hpp"
#include "intraday/IntradayUniverse.hpp"
#include "pairs/PriceMatrix.hpp"

namespace
{
    const double CAPITAL = 100000;
    const double PRICE = 100;
    const int MINUTES = 390;
    const std::string DAY = "2026-01-05";
    const size_t STOCK = 0, FUND = 1, OTHER = 2;   // price matrix rows in the day tests

    // "2026-01-05 09:30:00" plus `minute`
    std::string timeOf(int minute)
    {
        int clock = 9 * 60 + 30 + minute;
        char text[32];
        std::snprintf(text, sizeof(text), "%s %02d:%02d:00", DAY.c_str(), clock / 60, clock % 60);
        return text;
    }

    // One bar a minute at these closes, each opening at the close before
    std::vector<IntradayBar> minuteBars(const std::vector<double>& closes)
    {
        std::vector<IntradayBar> bars;
        for (size_t minute = 0; minute < closes.size(); minute++)
        {
            double open = minute == 0 ? closes[0] : closes[minute - 1];
            bars.push_back({timeOf(static_cast<int>(minute)), open, std::max(open, closes[minute]),
                            std::min(open, closes[minute]), closes[minute], 1000});
        }
        return bars;
    }

    IntradaySettings smallSettings()
    {
        IntradaySettings settings;
        settings.firstMinute = 30;
        settings.lastEntryMinute = 360;
        settings.stepMinutes = 5;
        settings.stocksEachSide = 1;
        settings.entryZ = 2.0;
        settings.exitZ = 0.0;
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

    IntradaySettings::Variant fade(int delay = 1)
    {
        IntradaySettings::Variant variant;
        variant.name = "fade";
        variant.lookbackMinutes = 30;
        variant.holdMinutes = 30;
        variant.delayMinutes = delay;
        return variant;
    }

    // A matrix with just the names the day tests trade
    PriceMatrix names()
    {
        PriceMatrix prices;
        prices.symbols = {"STOCK", "FUND", "OTHER"};
        return prices;
    }

    IntradayUniverse::Member member(size_t stock, double beta = 1.0)
    {
        IntradayUniverse::Member m;
        m.stock = stock;
        m.fund = FUND;
        m.beta = beta;
        m.dailySigma = 0.01;   // 1% a day: about 0.28% over 30 minutes
        return m;
    }

    // The stock 2% under its flat fund from minute 20 to minute 45, level otherwise
    std::vector<double> dipAndBack()
    {
        std::vector<double> closes(MINUTES, PRICE);
        for (int minute = 20; minute < 45; minute++)
            closes[minute] = PRICE * 0.98;
        return closes;
    }

    IntradayDay dayOf(const std::vector<double>& stock, const std::vector<double>& fund,
                      const std::vector<double>& other = std::vector<double>(MINUTES, PRICE))
    {
        AlpacaMultiBars::BarsBySymbol bars = {{"STOCK", minuteBars(stock)}, {"FUND", minuteBars(fund)},
                                              {"OTHER", minuteBars(other)}};
        return IntradayDay(bars, {STOCK, FUND, OTHER}, {"STOCK", "FUND", "OTHER"});
    }

    // Daily closes from returns
    std::vector<double> path(const std::vector<double>& returns)
    {
        std::vector<double> closes = {PRICE};
        for (double r : returns)
            closes.push_back(closes.back() * (1 + r));
        return closes;
    }
}

TEST(IntradayDayTests, CarriesThePriceOverQuietMinutesAndFillsAtTheNextOpen)
{
    std::vector<IntradayBar> bars = {{timeOf(0), 10, 10, 10, 10, 1}, {timeOf(3), 12, 13, 11, 12.5, 1}};
    IntradayDay day({{"A", bars}}, {7}, {"A"});
    EXPECT_TRUE(day.has(7));
    EXPECT_FALSE(day.has(8));
    EXPECT_EQ(day.price(7, 2), 10);     // nothing traded since the open
    EXPECT_EQ(day.price(7, 3), 12.5);
    EXPECT_EQ(day.price(7, 389), 12.5);
    EXPECT_EQ(day.fill(7, 3), 12);      // minute 3's open
    EXPECT_EQ(day.fill(7, 2), 10);      // no bar: the last price
    EXPECT_EQ(day.price(8, 3), 0);
}

TEST(IntradayUniverseTests, PicksRealStocksWithTheirClosestFund)
{
    // 30 days: two funds that move differently, a stock that follows the first at 1.5x plus its own noise, a
    // stock that's just the second fund (an index tracker), and a second share class of the first stock
    std::vector<double> fundA, fundB, stock, tracker, otherClass;
    for (int day = 0; day < 30; day++)
    {
        double a = std::sin(day * 1.3) * 0.01, b = std::cos(day * 0.7) * 0.01, own = std::sin(day * 2.9) * 0.012;
        fundA.push_back(a);
        fundB.push_back(b);
        stock.push_back(1.5 * a + own);
        tracker.push_back(b * 1.001);
        otherClass.push_back(1.5 * a + own * 1.01);
    }
    PriceMatrix prices;
    for (int day = 0; day <= 30; day++)
        prices.days.push_back("2026-01-" + std::string(day < 9 ? "0" : "") + std::to_string(day + 1));
    auto add = [&](const std::string& symbol, const std::vector<double>& returns, double volume) {
        std::vector<double> closes = path(returns);
        std::vector<quant::DailyBar> bars;
        for (size_t i = 0; i < closes.size(); i++)
            bars.push_back({prices.days[i], closes[i], closes[i], closes[i], closes[i], volume});
        prices.add(symbol, bars);
    };
    add("XLA", fundA, 1e8);
    add("XLB", fundB, 1e8);
    add("BIG", stock, 3e7);
    add("TRAK", tracker, 5e7);      // more dollar volume than BIG, but it's a fund
    add("BIGB", otherClass, 1e7);   // BIG's other class: less traded, so BIG is kept
    add("GOLD", fundB, 1e9);        // in not_stocks

    IntradaySettings settings;
    settings.formationDays = 30;
    settings.stocks = 10;
    settings.minPrice = 5;
    settings.maxCorrelation = 0.9;
    settings.funds = {"XLA", "XLB"};
    settings.notStocks = {"GOLD"};
    auto members = IntradayUniverse::pick(prices, settings, prices.days.size());
    ASSERT_EQ(members.size(), 1u);
    EXPECT_EQ(prices.symbols[members[0].stock], "BIG");
    EXPECT_EQ(prices.symbols[members[0].fund], "XLA");
    EXPECT_GT(members[0].beta, 1.2);
    EXPECT_LT(members[0].beta, 1.8);
    EXPECT_GT(members[0].dailySigma, 0);
}

TEST(IntradayBacktestTests, MeasuresTheGapAgainstTheFund)
{
    std::vector<double> stock(MINUTES, PRICE), fund(MINUTES, PRICE);
    for (int minute = 30; minute < MINUTES; minute++)
    {
        stock[minute] = PRICE * 1.01;   // up 1% ...
        fund[minute] = PRICE * 1.01;    // ... with its fund
    }
    IntradayDay bars = dayOf(stock, fund);
    double z = 0;
    ASSERT_TRUE(IntradayBacktest::gapZ(member(STOCK), bars, 0, 30, 30, z));
    EXPECT_NEAR(z, 0, 1e-9);   // no gap: it moved with its sector
    ASSERT_TRUE(IntradayBacktest::gapZ(member(STOCK, 0.0), bars, 0, 30, 30, z));
    EXPECT_NEAR(z, 0.01 / (0.01 * std::sqrt(30.0 / 390)), 1e-9);   // beta 0: the whole 1% is gap
    EXPECT_FALSE(IntradayBacktest::gapZ(member(STOCK), bars, -10, 20, 30, z));   // before the open
}

TEST(IntradayBacktestTests, FadesADipAndHedgesIt)
{
    IntradaySettings settings = smallSettings();
    PriceMatrix prices = names();
    IntradayBacktest backtest(settings, prices, noCosts());
    IntradayDay bars = dayOf(dipAndBack(), std::vector<double>(MINUTES, PRICE));
    IntradayBacktest::Members members = {member(STOCK)};
    std::vector<IntradayBacktest::Entry> entries;
    MorningBacktest::Line line;

    double pnl = backtest.day(fade(), members, bars, DAY, CAPITAL, false, entries, line);
    // At minute 30 the stock's 2% under: bought at minute 31's open (98), sold at the first look after it's
    // back level (minute 45), at minute 46's open (100). Then its 30-minute move is up 2%, so it's shorted at
    // 100 and bought back at 100 when that's gone. The fund didn't move, so the hedge made nothing.
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].side, 1);
    EXPECT_EQ(backtest.steps()[entries[0].exitStep], 45);
    EXPECT_EQ(entries[1].side, -1);
    double shares = std::floor(CAPITAL / 2 / (PRICE * 0.98));
    EXPECT_NEAR(pnl, shares * PRICE * 0.02, 1e-6);
    ASSERT_EQ(line.stockDays.size(), 3u);   // the two round trips, and the fund's day
    EXPECT_EQ(line.stockDays[0].trades, 1);
    EXPECT_EQ(line.stockDays[2].symbol, "FUND");
    EXPECT_EQ(line.stockDays[2].trades, 0);
    EXPECT_NEAR(line.stockDays[2].grossPnl, 0, 1e-6);
}

TEST(IntradayBacktestTests, TheHedgeTakesOutTheSectorsMove)
{
    // The stock dips with its fund and stays 2% under it; the fund keeps falling. Hedged, the fund's fall is
    // made back on the fund; unhedged, it isn't.
    std::vector<double> fund(MINUTES, PRICE), stock(MINUTES, PRICE);
    for (int minute = 20; minute < MINUTES; minute++)
    {
        fund[minute] = PRICE * (1 - 0.0005 * (minute - 19));
        stock[minute] = fund[minute] * 0.98;
    }
    IntradaySettings settings = smallSettings();
    PriceMatrix prices = names();
    IntradayBacktest backtest(settings, prices, noCosts());
    IntradayDay bars = dayOf(stock, fund);
    IntradayBacktest::Members members = {member(STOCK)};
    std::vector<IntradayBacktest::Entry> entries;
    MorningBacktest::Line hedged, unhedged;
    auto variant = fade();
    double withHedge = backtest.day(variant, members, bars, DAY, CAPITAL, false, entries, hedged);
    variant.hedge = false;
    double without = backtest.day(variant, members, bars, DAY, CAPITAL, false, entries, unhedged);
    EXPECT_LT(without, 0);
    EXPECT_GT(withHedge, without);
    EXPECT_NEAR(withHedge, 0, std::fabs(without) * 0.1);   // close to flat: the gap never closed
}

TEST(IntradayBacktestTests, IsFlatAtTheCloseAndStopsEnteringLate)
{
    IntradaySettings settings = smallSettings();
    settings.lastEntryMinute = 360;
    PriceMatrix prices = names();
    IntradayBacktest backtest(settings, prices, noCosts());
    std::vector<double> stock(MINUTES, PRICE);
    for (int minute = 340; minute < MINUTES; minute++)
        stock[minute] = PRICE * 0.97;   // dips at 15:10 and never comes back
    IntradayDay bars = dayOf(stock, std::vector<double>(MINUTES, PRICE));
    std::vector<IntradayBacktest::Entry> entries;
    MorningBacktest::Line line;
    auto variant = fade();
    variant.holdMinutes = 1000;
    backtest.day(variant, {member(STOCK)}, bars, DAY, CAPITAL, false, entries, line);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(backtest.steps()[entries[0].step], 340);
    EXPECT_EQ(entries[0].exitStep, backtest.steps().size() - 1);   // sold at 15:59
    EXPECT_EQ(line.stockDays[0].grossPnl, 0);                        // bought at 97, sold at 97
}

TEST(IntradayBacktestTests, TheControlCopiesTheEntriesWithRandomStocks)
{
    IntradaySettings settings = smallSettings();
    settings.stocksEachSide = 2;
    PriceMatrix prices = names();
    IntradayBacktest backtest(settings, prices, noCosts());
    IntradayDay bars = dayOf(dipAndBack(), std::vector<double>(MINUTES, PRICE));
    IntradayBacktest::Members members = {member(STOCK), member(OTHER)};
    std::vector<IntradayBacktest::Entry> entries;
    MorningBacktest::Line chosen, control;
    backtest.day(fade(), members, bars, DAY, CAPITAL, false, entries, chosen);
    ASSERT_FALSE(entries.empty());
    auto copied = entries;
    backtest.day(fade(), members, bars, DAY, CAPITAL, true, entries, control);
    EXPECT_EQ(entries.size(), copied.size());   // the control doesn't change them
    size_t trades = 0;
    for (const auto& trade : control.stockDays)
        trades += trade.trades;
    EXPECT_EQ(trades, copied.size());
}
