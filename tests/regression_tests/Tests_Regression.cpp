#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include "backtest/Backtester.hpp"
#include "strategy_engine/StrategyEngine.hpp"
#include "strategy_engine/StrategyFactory.hpp"

/*
 * Regression tests
 *
 * Run the whole pipeline on a fixed, made-up price series and check the results are exactly the
 * same as when these tests were written. Nothing here says the numbers are "good" - they pin
 * today's behaviour so any change to strategies, the OMS, the broker simulation or the metrics
 * shows up as a failing test.
 *
 * If you change behaviour on purpose (e.g. tune RSI), run:
 *     ./build/tests/regression_tests/regression_tests
 * read the new values from the "REGRESSION VALUES" line it prints, and update the EXPECT lines.
 */

namespace
{
    // Same series every run: two waves, a gentle trend and pseudo-random noise from a fixed seed
    std::vector<MarketCondition> makeMarket(int bars = 2000)
    {
        std::vector<MarketCondition> data;
        unsigned int seed = 12345;
        double previous = 200.0;

        for (int i = 0; i < bars; i++)
        {
            seed = seed * 1103515245u + 12345u;
            double noise = ((seed >> 16) % 1000) / 1000.0 - 0.5;   // -0.5 .. +0.5
            double close = 200.0 + 8.0 * std::sin(i / 45.0) + 3.0 * std::sin(i / 7.0) + 0.002 * i + noise;

            int day = i / 390, minute = i % 390;                     // 390 one-minute bars per trading day
            char time[32];
            std::snprintf(time, sizeof(time), "2026-01-%02d %02d:%02d:00", 5 + day, 9 + (30 + minute) / 60, (30 + minute) % 60);

            data.emplace_back(time, "NVDA", static_cast<float>(previous), static_cast<float>(close), 1000 + i, "1m");
            previous = close;
        }
        return data;
    }

    json makeConfig()
    {
        return {
            {"ticker", "NVDA"},
            {"max_position_size", 10},
            {"max_exposure", 5.0},
            {"slippage_tolerance", 2.0},
            {"strategies", {{
                {"name", "RSI"}, {"active", 1}, {"period", 14},
                {"overbought_threshold", 70.0}, {"oversold_threshold", 30.0},
                {"stop_loss", 2}, {"take_profit", 2}
            }}}
        };
    }

    // Broker for the live-path test: fills instantly at the order price and records every order
    class RecordingBroker : public BrokerBase
    {
        public:
            std::vector<Order> orders;
            float held = 0;

            int connect() override { return 1; }
            int disconnect() override { return 1; }
            float getLatestPrice(std::string) override { return 0; }
            int placeOrder(Order order) override
            {
                orders.push_back(order);
                held += order.isBuy() ? order.getQuantity() : -order.getQuantity();
                return 1;
            }
            Position getLatestPosition(std::string ticker) override { return Position(ticker, held, 0); }
    };
}

TEST(RegressionTests, FullBacktestResultsUnchanged)
{
    json config = makeConfig();
    std::vector<MarketCondition> market = makeMarket();

    Backtester backtester(config);
    backtester.setStartingCapital(100000.0);
    backtester.setCommissionPerTrade(1.0);
    backtester.setSlippagePercentage(0.0);   // no randomness
    backtester.setMarketData(market);
    backtester.run();

    const PerformanceMetrics& m = backtester.getPerformanceMetrics();
    std::printf("REGRESSION VALUES backtest: fills=%d finalEquity=%.2f realised=%.2f unrealised=%.2f "
                "commission=%.2f wins=%d losses=%d maxDD=%.4f sharpe=%.4f\n",
                m.numTrades, m.finalEquity, m.realisedPnL, m.unrealisedPnL, m.commissionPaid,
                m.winningTrades, m.losingTrades, m.maxDrawdownPercent, m.sharpeRatio);

    // Pinned results (see the comment at the top before changing these)
    EXPECT_EQ(m.numTrades, 2770);
    EXPECT_NEAR(m.finalEquity, 97243.66, 0.01);
    EXPECT_NEAR(m.realisedPnL, 13.66, 0.01);
    EXPECT_NEAR(m.unrealisedPnL, 0.00, 0.01);
    EXPECT_NEAR(m.commissionPaid, 2770.00, 0.01);
    EXPECT_EQ(m.winningTrades, 3);
    EXPECT_EQ(m.losingTrades, 0);
    EXPECT_NEAR(m.maxDrawdownPercent, 2.7622, 1e-3);
    EXPECT_NEAR(m.sharpeRatio, -452.4447, 1e-2);

    // Books must balance: P&L = realised + unrealised - commission
    EXPECT_NEAR(m.totalPnL, m.realisedPnL + m.unrealisedPnL - m.commissionPaid, 0.01);
}

TEST(RegressionTests, LiveDecisionPathUnchanged)
{
    json config = makeConfig();
    std::vector<MarketCondition> market = makeMarket();

    MarketData marketData;
    StrategyFactory factory(config);
    StrategyEngine engine;
    RecordingBroker broker;
    engine.setUp(config, factory, marketData, &broker);

    // Feed bars one at a time exactly like the live app does
    for (const auto& bar : market)
    {
        marketData.addBar(bar, 500);
        engine.run();
    }

    int buys = 0, sells = 0;
    for (const auto& order : broker.orders)
        (order.isBuy() ? buys : sells)++;

    std::printf("REGRESSION VALUES live: orders=%zu buys=%d sells=%d first=", broker.orders.size(), buys, sells);
    for (size_t i = 0; i < std::min<size_t>(5, broker.orders.size()); i++)
        std::printf("%s@%.4f ", broker.orders[i].getTypeAsString().c_str(), broker.orders[i].getPrice());
    std::printf("\n");

    // Pinned results (see the comment at the top before changing these)
    EXPECT_EQ(broker.orders.size(), 1367u);
    EXPECT_EQ(buys, 660);
    EXPECT_EQ(sells, 707);
    ASSERT_GE(broker.orders.size(), 5u);
    std::vector<float> firstPrices = {204.7699f, 205.5798f, 205.0379f, 204.5833f, 205.4272f};
    for (size_t i = 0; i < firstPrices.size(); i++) {
        EXPECT_EQ(broker.orders[i].getTypeAsString(), "SELL") << "order " << i;
        EXPECT_NEAR(broker.orders[i].getPrice(), firstPrices[i], 1e-3) << "order " << i;
    }
}
