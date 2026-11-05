#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include "../../src/strategy_engine/SCALP.hpp"
#include "../../src/strategy_engine/StrategyFactory.hpp"

namespace
{
    StrategyAttribute scalpSettings(double entry = 2.0, double exit = 0.0, int maxHold = 10, int warmup = 15)
    {
        return StrategyAttribute(json{{"name", "SCALP"}, {"entry_threshold", entry}, {"exit_threshold", exit},
                                      {"max_hold_bars", maxHold}, {"warmup_minutes", warmup},
                                      {"stop_loss", 0.3}, {"take_profit", 1.0}});
    }

    std::string at(int minute, const std::string& day = "2026-11-05")
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%s %02d:%02d:00", day.c_str(), minute / 60, minute % 60);
        return text;
    }

    // Feeds bars one at a time like the engine, telling the strategy the position it holds
    // (following its own orders unless `position` is overridden). Returns the order raised on each bar.
    struct Runner
    {
        SCALP scalp{scalpSettings()};
        MarketData data;
        float position = 0;
        bool followOrders = true;

        explicit Runner(StrategyAttribute settings) : scalp(settings) {}

        std::string bar(int minute, float close, int volume = 1000, const std::string& day = "2026-11-05")
        {
            data.addBar(MarketCondition(at(minute, day), "TEST", close, close, volume, "1m"), 500);
            scalp.supplyData(data);
            scalp.setCurrentPosition(position);
            scalp.execute();
            if (!scalp.onNewOrder())
                return "";
            Order order = scalp.getOrder();
            if (followOrders)
                position += order.isBuy() ? order.getQuantity() : -order.getQuantity();
            return order.getTypeAsString();
        }

        // A quiet session from 09:30: alternating 100.1 / 99.9 for `minutes` minutes (VWAP 100, sigma 0.1)
        void quietUntil(int endMinute, const std::string& day = "2026-11-05")
        {
            for (int m = MarketTime::OPEN_MINUTE; m < endMinute; m++)
                bar(m, (m % 2) ? 100.1f : 99.9f, 1000, day);
        }
    };

    constexpr int T1000 = 10 * 60;
}

TEST(SCALPTests, VwapAndBandsAreVolumeWeighted)
{
    Runner r(scalpSettings());
    r.bar(MarketTime::OPEN_MINUTE, 100.0f, 1000);
    r.bar(MarketTime::OPEN_MINUTE + 1, 103.0f, 3000);   // 3x the volume
    // VWAP = (100 x 1000 + 103 x 3000) / 4000 = 102.25
    EXPECT_NEAR(r.scalp.vwap(), 102.25, 1e-9);
    // sigma^2 = (1000 x 100^2 + 3000 x 103^2) / 4000 - 102.25^2 = 1.6875
    EXPECT_NEAR(r.scalp.sigma(), std::sqrt(1.6875), 1e-6);
}

TEST(SCALPTests, FadesAStretchAndTakesProfitBackAtVwap)
{
    Runner r(scalpSettings(2.0, 0.0, 30));
    r.quietUntil(T1000);   // VWAP ~100, sigma ~0.1 -> bands ~99.8 / 100.2

    EXPECT_EQ(r.bar(T1000, 100.5f), "SELL");   // stretched up: short it
    EXPECT_EQ(r.scalp.getState(), -1);
    EXPECT_EQ(r.bar(T1000 + 1, 100.4f), "");   // still above VWAP: hold
    EXPECT_EQ(r.bar(T1000 + 2, 99.95f), "BUY");   // back to VWAP: close
    EXPECT_EQ(r.scalp.getState(), 0);
    EXPECT_FLOAT_EQ(r.position, 0);

    EXPECT_EQ(r.bar(T1000 + 3, 99.3f), "BUY");   // stretched down: long
    EXPECT_EQ(r.scalp.getState(), 1);
}

TEST(SCALPTests, ExitThresholdTakesProfitBeforeVwap)
{
    Runner r(scalpSettings(3.0, 1.0, 30));
    r.quietUntil(T1000);
    double vwap = r.scalp.vwap(), sigma = r.scalp.sigma();
    EXPECT_EQ(r.bar(T1000, static_cast<float>(vwap - 4 * sigma)), "BUY");
    // Target is VWAP - 1 sigma (the new bar moves the VWAP a little, so aim just inside)
    EXPECT_EQ(r.bar(T1000 + 1, static_cast<float>(r.scalp.vwap() - 0.8 * r.scalp.sigma())), "SELL");
}

TEST(SCALPTests, TimeLimitClosesASlowTrade)
{
    Runner r(scalpSettings(2.0, 0.0, 5));
    r.quietUntil(T1000);
    EXPECT_EQ(r.bar(T1000, 99.0f), "BUY");
    for (int i = 1; i < 5; i++)
        EXPECT_EQ(r.bar(T1000 + i, 99.0f), "") << i;   // still stretched, not back to VWAP
    EXPECT_EQ(r.bar(T1000 + 5, 99.0f), "SELL");          // 5 bars held: out
}

TEST(SCALPTests, NoEntriesDuringWarmupOrNearTheClose)
{
    Runner r(scalpSettings(2.0, 0.0, 1000, 15));
    for (int m = MarketTime::OPEN_MINUTE; m < MarketTime::OPEN_MINUTE + 15; m++)
        EXPECT_EQ(r.bar(m, (m % 2) ? 101.0f : 99.0f), "") << "minute " << m;   // big swings, but warming up

    Runner late(scalpSettings());
    late.quietUntil(SCALP::LAST_ENTRY_MINUTE);
    EXPECT_EQ(late.bar(SCALP::LAST_ENTRY_MINUTE, 105.0f), "");   // 15:45: no new trades
}

TEST(SCALPTests, FlatByTheClose)
{
    Runner r(scalpSettings(2.0, 0.0, 1000));
    r.quietUntil(15 * 60 + 40);
    EXPECT_EQ(r.bar(15 * 60 + 40, 101.0f), "SELL");
    for (int m = 15 * 60 + 41; m < SCALP::FLAT_MINUTE; m++)
        EXPECT_EQ(r.bar(m, 101.0f), "");
    EXPECT_EQ(r.bar(SCALP::FLAT_MINUTE, 101.0f), "BUY");   // 15:55: close regardless
    EXPECT_FLOAT_EQ(r.position, 0);
}

TEST(SCALPTests, NoticesTheBrokerClosedTheTrade)
{
    Runner r(scalpSettings(2.0, 0.0, 30));
    r.quietUntil(T1000);
    EXPECT_EQ(r.bar(T1000, 100.6f), "SELL");
    r.position = 0;   // the broker's stop loss bought it back
    EXPECT_EQ(r.bar(T1000 + 1, 99.95f), "");   // back at VWAP, but nothing to close: no stray BUY
    EXPECT_EQ(r.scalp.getState(), 0);
}

TEST(SCALPTests, IgnoresPreMarketAndStartsFreshEachDay)
{
    Runner r(scalpSettings());
    // Pre-market prints far away from the session's prices don't count towards the VWAP
    for (int m = 8 * 60; m < 8 * 60 + 30; m++)
        EXPECT_EQ(r.bar(m, 150.0f), "");
    r.quietUntil(T1000);
    EXPECT_NEAR(r.scalp.vwap(), 100.0, 0.01);

    // Next day: the VWAP is rebuilt from that day's open
    r.quietUntil(T1000, "2026-11-06");
    r.bar(T1000, 120.0f, 1000, "2026-11-06");
    EXPECT_NEAR(r.scalp.vwap(), (30 * 100.0 + 120.0) / 31, 0.01);   // 30 quiet bars at ~100 and one at 120
}

TEST(SCALPTests, RebuildsVwapAfterABackFill)
{
    // The live app back-fills bars, then the strategy runs for the first time mid-session
    Runner r(scalpSettings());
    for (int m = MarketTime::OPEN_MINUTE; m < T1000; m++)
        r.data.addBar(MarketCondition(at(m), "TEST", 100.0f + (m % 2), 100.0f + (m % 2), 1000, "1m"), 500);
    r.bar(T1000, 100.5f);
    EXPECT_NEAR(r.scalp.vwap(), 100.5, 0.02);   // built from every bar since the open
}

TEST(SCALPTests, StatusShowsBandsOrTheTrade)
{
    Runner r(scalpSettings(2.0, 0.0, 10));
    EXPECT_EQ(r.scalp.status(), "");
    r.quietUntil(T1000);
    EXPECT_NE(r.scalp.status().find("SCALP   VWAP 100.00 | L: 99.80 | C: 100.10 | U: 100.20"), std::string::npos)
        << r.scalp.status();
    r.bar(T1000, 100.6f);
    EXPECT_NE(r.scalp.status().find("short 0/10 bars, C: 100.60 | target <= "), std::string::npos) << r.scalp.status();
}

TEST(SCALPTests, FactoryAndValidation)
{
    json config = {{"strategies", {{{"name", "SCALP"}, {"active", 1}, {"entry_threshold", 2.0}, {"exit_threshold", 0.0},
                                    {"max_hold_bars", 10}, {"stop_loss", 0.3}, {"take_profit", 1.0}}}}};
    StrategyFactory factory(config);
    auto strategies = factory.generateStrategies();
    ASSERT_EQ(strategies.size(), 1u);
    EXPECT_EQ(strategies[0]->_strategyAttribute.name, "SCALP");
    EXPECT_EQ(strategies[0]->_strategyAttribute.warmup_minutes, 15);   // default
    EXPECT_EQ(strategies[0]->barsNeeded(), 391);

    EXPECT_THROW(SCALP(scalpSettings(0.0)).validate(), std::runtime_error);        // no entry level
    EXPECT_THROW(SCALP(scalpSettings(2.0, 2.0)).validate(), std::runtime_error);   // exit not inside entry
    EXPECT_THROW(SCALP(scalpSettings(2.0, 0.0, 0)).validate(), std::runtime_error);
    EXPECT_NO_THROW(SCALP(scalpSettings()).validate());
}
