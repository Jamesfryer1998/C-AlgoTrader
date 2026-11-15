#include <gtest/gtest.h>
#include <cmath>
#include "swing/SwingBacktest.hpp"
#include "swing/SwingIndicators.hpp"
#include "swing/SwingSettings.hpp"
#include "pairs/PriceMatrix.hpp"

namespace
{
    const double VOLUME = 1e6;    // shares a day: $10 million or more at these prices
    const double FLAT = 50;
    const double CAPITAL = 100000;

    // Weekdays from 2026-01-05 (a Monday) on; holidays don't matter here
    std::vector<std::string> weekdays(int count)
    {
        std::vector<std::string> days;
        int month = 1, day = 5, weekday = 1;
        static const int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        while (static_cast<int>(days.size()) < count)
        {
            if (weekday >= 1 && weekday <= 5)
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

    // Opens at the close before, high and low 1% either side unless given
    void addStock(PriceMatrix& prices, const std::string& symbol, const std::vector<double>& closes,
                  const std::vector<double>& volumes = {}, const std::vector<double>& lows = {})
    {
        std::vector<quant::DailyBar> bars;
        for (size_t i = 0; i < closes.size() && i < prices.days.size(); i++)
        {
            double open = i == 0 ? closes[i] : closes[i - 1];
            double low = lows.empty() ? std::min(open, closes[i]) * 0.99 : lows[i];
            bars.push_back({prices.days[i], open, std::max(open, closes[i]) * 1.01, low, closes[i],
                            volumes.empty() ? VOLUME : volumes[i]});
        }
        prices.add(symbol, bars);
    }

    SwingSettings smallSettings()
    {
        SwingSettings settings;
        settings.minPrice = 5;
        settings.minDollarVolume = 1e6;
        settings.dollarVolumeDays = 5;
        settings.trendDays = 20;
        settings.rsiDays = 2;
        settings.rsiBelow = 10;
        settings.exitAboveDays = 5;
        settings.pullbackMaxHold = 10;
        settings.highDays = 20;
        settings.volumeDays = 10;
        settings.volumeRatio = 1.5;
        settings.atrDays = 5;
        settings.stopAtr = 2.0;
        settings.breakoutHold = 10;
        settings.returnDays = 5;
        settings.losersHold = 5;
        settings.positions = 2;
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

    MorningBacktest::Line trade(const SwingSettings& settings, const PriceMatrix& prices,
                                const SwingSettings::Variant& variant, bool chosen = true)
    {
        SwingIndicators indicators(prices, settings);
        SwingBacktest backtest(settings, prices, indicators, noCosts());
        MorningBacktest::Line line;
        line.equity = CAPITAL;
        backtest.run(variant, chosen, 0, prices.days.size() - 1, line);
        return line;
    }

    SwingSettings::Variant variantOf(SwingSettings::Kind kind)
    {
        SwingSettings::Variant variant;
        variant.name = "test";
        variant.kind = kind;
        return variant;
    }

    // Up a little every day for `days`, wiggling so the RSI isn't stuck at 100
    std::vector<double> uptrend(int days)
    {
        std::vector<double> closes;
        for (int i = 0; i < days; i++)
            closes.push_back(FLAT + i * 0.5 + (i % 2 ? 0.2 : 0));
        return closes;
    }
}

TEST(SwingIndicatorsTests, AveragesHighsAndChangesNeedEveryBar)
{
    std::vector<double> values = {1, 2, 3, 4, 0, 6, 7, 8};
    auto average = SwingIndicators::average(values, 3);
    EXPECT_EQ(average[1], 0);                   // not enough history
    EXPECT_FLOAT_EQ(average[2], 2);
    EXPECT_FLOAT_EQ(average[3], 3);
    EXPECT_EQ(average[5], 0);                   // a missing bar in the window
    EXPECT_FLOAT_EQ(average[7], 7);

    auto prior = SwingIndicators::priorAverage(values, 3);
    EXPECT_FLOAT_EQ(prior[3], 2);               // the three days before
    auto highest = SwingIndicators::priorMax({5, 9, 3, 4, 2, 1}, 3);
    EXPECT_EQ(highest[2], 0);
    EXPECT_FLOAT_EQ(highest[3], 9);
    EXPECT_FLOAT_EQ(highest[4], 9);
    EXPECT_FLOAT_EQ(highest[5], 4);             // 9 has left the window

    auto change = SwingIndicators::change({10, 11, 12, 0, 15}, 2);
    EXPECT_FLOAT_EQ(change[2], 0.2f);
    EXPECT_EQ(change[3], 0);
}

TEST(SwingIndicatorsTests, RsiAndAtrFollowWilder)
{
    auto rsi = SwingIndicators::wilderRsi({10, 11, 12, 11, 10}, 2);
    EXPECT_EQ(rsi[1], 0);                       // one change isn't enough for two days
    EXPECT_FLOAT_EQ(rsi[2], 100);               // only gains
    // Gains (1, 1) then a 1 loss: gain (1 + 0) / 2 = 0.5, loss (0 + 1) / 2 = 0.5, RSI 50
    EXPECT_FLOAT_EQ(rsi[3], 50);
    // Another 1 loss: gain 0.25, loss 0.75, RSI 25
    EXPECT_FLOAT_EQ(rsi[4], 25);

    // True range 2 every day: ATR 2
    auto atr = SwingIndicators::wilderAtr({11, 11, 11, 11}, {9, 9, 9, 9}, {10, 10, 10, 10}, 2);
    EXPECT_EQ(atr[1], 0);
    EXPECT_FLOAT_EQ(atr[2], 2);
}

TEST(SwingBacktestTests, KnowsTheWeekdaysAndTheWeeksLastDay)
{
    EXPECT_EQ(SwingBacktest::weekday("2026-01-05"), 1);   // Monday
    EXPECT_EQ(SwingBacktest::weekday("2026-01-09"), 5);   // Friday
    EXPECT_EQ(SwingBacktest::weekday("2024-02-29"), 4);   // a leap day, Thursday
    std::vector<std::string> days = {"2026-01-08", "2026-01-09", "2026-01-12", "2026-01-15", "2026-01-20"};
    EXPECT_FALSE(SwingBacktest::lastOfWeek(days, 0));
    EXPECT_TRUE(SwingBacktest::lastOfWeek(days, 1));      // Friday
    EXPECT_FALSE(SwingBacktest::lastOfWeek(days, 2));
    EXPECT_TRUE(SwingBacktest::lastOfWeek(days, 3));      // Thursday before a long weekend
    EXPECT_TRUE(SwingBacktest::lastOfWeek(days, 4));      // the last day there is
}

TEST(SwingBacktestTests, BuysAPullbackAtTheNextOpenAndSellsOnTheBounce)
{
    SwingSettings settings = smallSettings();
    PriceMatrix prices;
    prices.days = weekdays(40);
    std::vector<double> closes = uptrend(30);
    double top = closes.back();
    for (double drop : {top - 2, top - 4, top - 3, top + 1, top + 2, top + 3, top + 4, top + 5, top + 6, top + 7})
        closes.push_back(drop);
    addStock(prices, "DIP", closes);

    MorningBacktest::Line line = trade(settings, prices, variantOf(SwingSettings::Kind::Pullback));
    ASSERT_EQ(line.stockDays.size(), 1u);
    // Signal at day 31's close (two down days); bought at day 32's open (day 31's close), sold at the open after
    // the first close above its 5-day average
    EXPECT_EQ(line.stockDays[0].day, prices.days[32]);
    EXPECT_GT(line.stockDays[0].grossPnl, 0);
    double shares = std::floor(CAPITAL / settings.positions / (top - 4));
    EXPECT_NEAR(line.stockDays[0].grossPnl, shares * (prices.open[0][34] - (top - 4)), 1e-6);
    EXPECT_NEAR(line.equity, CAPITAL + line.stockDays[0].grossPnl, 1e-6);
}

TEST(SwingBacktestTests, NoPullbackBelowTheTrend)
{
    SwingSettings settings = smallSettings();
    PriceMatrix prices;
    prices.days = weekdays(40);
    std::vector<double> closes;
    for (int i = 0; i < 40; i++)
        closes.push_back(80 - i);   // falling: every RSI is low, but it's under its average
    addStock(prices, "FALL", closes);
    EXPECT_TRUE(trade(settings, prices, variantOf(SwingSettings::Kind::Pullback)).stockDays.empty());
}

TEST(SwingBacktestTests, ABreakoutIsStoppedOutAtTheStopOrAGappedOpen)
{
    SwingSettings settings = smallSettings();
    PriceMatrix prices;
    prices.days = weekdays(40);
    std::vector<double> closes(25, FLAT), volumes(25, VOLUME);
    closes.push_back(FLAT + 5);           // day 25: a new high on three times the volume
    volumes.push_back(3 * VOLUME);
    for (int i = 26; i < 40; i++)
    {
        closes.push_back(i < 28 ? FLAT + 5 : FLAT - 10);   // day 28 gaps far under the stop
        volumes.push_back(VOLUME);
    }
    addStock(prices, "HIGH", closes, volumes);

    MorningBacktest::Line line = trade(settings, prices, variantOf(SwingSettings::Kind::Breakout));
    ASSERT_EQ(line.stockDays.size(), 1u);
    EXPECT_EQ(line.stockDays[0].day, prices.days[26]);
    // Bought at day 26's open (FLAT + 5); day 28 opens at FLAT + 5 and closes at FLAT - 10, so the low passes the
    // stop and it's sold at the stop, under the entry
    double shares = std::floor(CAPITAL / settings.positions / (FLAT + 5));
    EXPECT_LT(line.stockDays[0].grossPnl, 0);
    EXPECT_GT(line.stockDays[0].grossPnl, shares * -15);   // the stop saved some of the fall
}

TEST(SwingBacktestTests, BuysTheWeeksLosersOnFridayAndHoldsThemFiveDays)
{
    SwingSettings settings = smallSettings();
    settings.positions = 1;
    settings.returnDays = 4;      // Monday's close to Friday's (the first week has no Friday before it)
    PriceMatrix prices;
    prices.days = weekdays(15);   // Monday 5th to Friday 23rd
    std::vector<double> loser(15, FLAT), flat(15, FLAT), smaller(15, FLAT);
    for (int i = 0; i < 15; i++)
    {
        loser[i] = FLAT - 2 * i;      // down $8 a week
        smaller[i] = FLAT - i;        // down $4
    }
    addStock(prices, "LOSER", loser);
    addStock(prices, "FLAT", flat);
    addStock(prices, "SMALLER", smaller);

    MorningBacktest::Line line = trade(settings, prices, variantOf(SwingSettings::Kind::Losers));
    ASSERT_FALSE(line.stockDays.empty());
    EXPECT_EQ(line.stockDays[0].symbol, "LOSER");           // the biggest loser, one place
    EXPECT_EQ(line.stockDays[0].day, prices.days[5]);       // bought Monday the 12th
    // Five days held: sold at Friday the 16th's close, so the next week's pick is bought on Monday the 19th
    ASSERT_EQ(line.stockDays.size(), 2u);
    EXPECT_EQ(line.stockDays[1].day, prices.days[10]);
}

TEST(SwingBacktestTests, NoMorePositionsThanPlaces)
{
    SwingSettings settings = smallSettings();
    settings.positions = 1;
    PriceMatrix prices;
    prices.days = weekdays(15);
    std::vector<double> a(15), b(15);
    for (int i = 0; i < 15; i++)
    {
        a[i] = FLAT - i;
        b[i] = FLAT - 0.5 * i;
    }
    addStock(prices, "A", a);
    addStock(prices, "B", b);
    MorningBacktest::Line line = trade(settings, prices, variantOf(SwingSettings::Kind::Losers));
    for (size_t i = 1; i < line.stockDays.size(); i++)
        EXPECT_NE(line.stockDays[i].day, line.stockDays[i - 1].day);   // one at a time
}

TEST(SwingBacktestTests, TheControlBuysAsManyRandomStocksOnTheSameDays)
{
    SwingSettings settings = smallSettings();
    settings.rsiBelow = 30;   // any short dip will do here
    settings.positions = 3;
    PriceMatrix prices;
    prices.days = weekdays(60);
    for (int stock = 0; stock < 8; stock++)
    {
        std::vector<double> closes;
        int dip = 25 + 3 * stock;   // two down days, at a different time for each
        for (int i = 0; i < 60; i++)
            closes.push_back(FLAT + stock + i * 0.5 - (i == dip ? 2 : 0) - (i == dip + 1 ? 4 : 0));
        addStock(prices, "S" + std::to_string(stock), closes);
    }
    SwingIndicators indicators(prices, settings);
    SwingBacktest backtest(settings, prices, indicators, noCosts());
    auto variant = variantOf(SwingSettings::Kind::Pullback);
    size_t last = prices.days.size() - 1;

    auto countBuys = [&](const SwingSettings::Variant& tried, const SwingBacktest::Signals& picks,
                         const std::vector<size_t>* quotas, SwingBacktest::Signals& bought) {
        MorningBacktest::Line line;
        line.equity = CAPITAL;
        backtest.trade(tried, picks, quotas, 0, last, line, bought);
        return line.stockDays.size();
    };
    SwingBacktest::Signals bought, boughtAtRandom;
    size_t trades = countBuys(variant, backtest.signals(variant, 0, last), nullptr, bought);
    EXPECT_GT(trades, 0u);
    std::vector<size_t> quotas;
    for (const auto& today : bought)
        quotas.push_back(today.size());
    countBuys(variant, backtest.randomSignals(variant, bought, 0, last), &quotas, boughtAtRandom);
    for (size_t i = 0; i < bought.size(); i++)
        EXPECT_EQ(bought[i].size(), boughtAtRandom[i].size()) << prices.days[i];

    // Limited to one symbol: as many buys of it, on random days
    variant.symbols = {"S3"};
    size_t ownTrades = countBuys(variant, backtest.signals(variant, 0, last), nullptr, bought);
    auto random = backtest.randomSignals(variant, bought, 0, last);
    size_t randomBuys = 0;
    for (const auto& today : random)
        for (size_t stock : today)
        {
            EXPECT_EQ(prices.symbols[stock], "S3");
            randomBuys++;
        }
    EXPECT_GT(ownTrades, 0u);
    EXPECT_EQ(randomBuys, ownTrades);
}
