#include <gtest/gtest.h>
#include "../../src/strategy_engine/StrategyEngine.hpp"
#include "../../src/broker/SimulatedBroker.hpp"


class StrategyEngineTests : public ::testing::Test {
public:

    StrategyEngine cut;
    Config config;

    void SetUp() override 
    {
        config.loadJson(config.getTestPath("strategy_tests/test_data/config_test.json"));
        jsonConfig = config.loadConfig();
    }

    void TearDown() override 
    {

    }

    json jsonConfig;
};

TEST_F(StrategyEngineTests, CanInstantiate)
{
    StrategyEngine cut;
}

TEST_F(StrategyEngineTests, CanSetUpStrategyEngine)
{
    // Use consolidated config file
    string stratFilePath = config.getTestPath("strategy_tests/test_data/strategies_consolidated.json");
    MarketData marketData;
    StrategyFactory stratFactory(stratFilePath);
    SimulatedBroker broker(marketData);
    cut.setUp(jsonConfig, stratFactory, marketData, &broker);
}


// When it comes to testing strat engine correclty makes orders

// TEST_F(StrategyEngineTests, CanGetOmsFromStrategyEngine)
// {
//     string stratFilePath = config.getTestPath("strategy_tests/test_data/strategies_consolidated.json");
//     MarketData marketData;
//     StrategyFactory stratFactory(stratFilePath);
//     SimulatedBroker broker(marketData);
//     cut.setUp(jsonConfig, stratFactory, marketData, &broker);
//     auto oms = cut.getOms();
// }

// ---------- Strategies must agree before an order is sent ----------

TEST(StrategyConsensusTests, NoProposalsMeansNoOrder)
{
    Order chosen;
    EXPECT_FALSE(StrategyEngine::chooseOrder({}, chosen));
}

TEST(StrategyConsensusTests, SingleStrategyOrderIsSent)
{
    Order chosen;
    std::vector<Order> proposals = {Order(OrderType::BUY, "NVDA", 1, 100)};
    ASSERT_TRUE(StrategyEngine::chooseOrder(proposals, chosen));
    EXPECT_EQ(chosen.getTypeAsString(), "BUY");
}

TEST(StrategyConsensusTests, AgreeingStrategiesSendOneOrder)
{
    Order chosen;
    std::vector<Order> proposals = {Order(OrderType::SELL, "NVDA", 1, 100), Order(OrderType::SELL, "NVDA", 2, 101)};
    ASSERT_TRUE(StrategyEngine::chooseOrder(proposals, chosen));
    EXPECT_EQ(chosen.getTypeAsString(), "SELL");
    EXPECT_FLOAT_EQ(chosen.getQuantity(), 1); // the first strategy's order, sent once
}

TEST(StrategyConsensusTests, DisagreeingStrategiesSendNothing)
{
    Order chosen;
    std::vector<Order> proposals = {Order(OrderType::BUY, "NVDA", 1, 100), Order(OrderType::SELL, "NVDA", 1, 100)};
    EXPECT_FALSE(StrategyEngine::chooseOrder(proposals, chosen));
}

TEST(StrategyConsensusTests, LimitBuyAgreesWithMarketBuy)
{
    Order chosen;
    std::vector<Order> proposals = {Order(OrderType::BUY, "NVDA", 1, 100), Order(OrderType::LIMIT_BUY, "NVDA", 1, 99)};
    EXPECT_TRUE(StrategyEngine::chooseOrder(proposals, chosen));
}

// ---------- Several strategies, one bar: at most one order reaches the broker ----------

namespace
{
    class CountingBroker : public BrokerBase
    {
        public:
            std::vector<Order> orders;
            int connect() override { return 1; }
            int disconnect() override { return 1; }
            float getLatestPrice(std::string) override { return 0; }
            int placeOrder(Order order) override { orders.push_back(order); return 1; }
            Position getLatestPosition(std::string ticker) override { return Position(ticker, 0, 0); }
    };

    // RSI(5) and mean reversion both see a sharp dip as a BUY signal on the same bar
    json twoStrategies(bool meanRevFlipped = false)
    {
        json rsi = {{"name", "RSI"}, {"active", 1}, {"period", 5}, {"overbought_threshold", 70.0},
                    {"oversold_threshold", 30.0}, {"stop_loss", 5}, {"take_profit", 5}};
        json meanrev = {{"name", "MEANREV"}, {"active", 1}, {"lookback_period", 10}, {"moving_average_period", 10},
                        {"entry_threshold", 1.5}, {"exit_threshold", 0.25}, {"stop_loss", 5}, {"take_profit", 5}};
        json config = {{"ticker", "NVDA"}, {"max_position_size", 10}, {"max_exposure", 5.0},
                       {"slippage_tolerance", 2.0}, {"strategies", {rsi, meanrev}}};
        return config;
    }

    // Quiet market around 100, then one sharp dip on the last bar
    std::vector<MarketCondition> quietThenDip()
    {
        std::vector<MarketCondition> bars;
        for (int i = 0; i < 30; i++)
        {
            float close = 100.0f + (i % 2 ? 0.2f : -0.2f);
            bars.emplace_back("2026-10-30 10:" + std::string(i < 10 ? "0" : "") + std::to_string(i) + ":00", "NVDA", close, close, 100, "1m");
        }
        bars.emplace_back("2026-10-30 10:30:00", "NVDA", 100.0f, 97.0f, 100, "1m");
        return bars;
    }
}

TEST(StrategyEngineConsensusTests, TwoStrategiesAgreeingSendOneOrderNotTwo)
{
    json config = twoStrategies();
    StrategyFactory factory(config);
    StrategyEngine engine;
    CountingBroker broker;
    MarketData data;
    engine.setUp(config, factory, data, &broker);

    for (const auto& bar : quietThenDip())
    {
        data.addBar(bar);
        engine.run();
    }

    // Both strategies wanted to BUY on the dip; the broker must see exactly one order
    ASSERT_EQ(broker.orders.size(), 1u);
    EXPECT_EQ(broker.orders[0].getTypeAsString(), "BUY");
    EXPECT_FLOAT_EQ(broker.orders[0].getQuantity(), 1);
    EXPECT_EQ(broker.orders[0].getStrategy(), "RSI+MEANREV");   // both are credited
}

TEST(StrategyEngineConsensusTests, EachStrategyAloneWouldHaveTraded)
{
    // Sanity check for the test above: on its own, each strategy does signal on the dip
    for (const char* name : {"RSI", "MEANREV"})
    {
        json config = twoStrategies();
        json only = json::array();
        for (const auto& s : config["strategies"])
            if (s["name"] == name)
                only.push_back(s);
        config["strategies"] = only;

        StrategyFactory factory(config);
        StrategyEngine engine;
        CountingBroker broker;
        MarketData data;
        engine.setUp(config, factory, data, &broker);
        for (const auto& bar : quietThenDip())
        {
            data.addBar(bar);
            engine.run();
        }
        EXPECT_EQ(broker.orders.size(), 1u) << name;
    }
}

// ---------- Several stocks: one engine each, one shared OMS ----------

namespace
{
    std::vector<MarketCondition> renamed(std::vector<MarketCondition> bars, const std::string& ticker)
    {
        for (auto& bar : bars)
            bar.Ticker = ticker;
        return bars;
    }

    std::vector<MarketCondition> quiet(const std::string& ticker)
    {
        std::vector<MarketCondition> bars = quietThenDip();
        bars.pop_back();   // no dip
        return renamed(bars, ticker);
    }
}

TEST(SharedOmsTests, EachStockTradesOnItsOwnPricesThroughOneOms)
{
    json config = twoStrategies();
    CountingBroker broker;
    OrderManagement shared;
    shared.setUp(config, &broker);

    // Two lanes, as the live app builds them
    json aaaConfig = config, bbbConfig = config, cccConfig = config;
    aaaConfig["ticker"] = "AAA";
    bbbConfig["ticker"] = "BBB";
    cccConfig["ticker"] = "CCC";
    StrategyFactory aaaFactory(aaaConfig), bbbFactory(bbbConfig), cccFactory(cccConfig);
    StrategyEngine aaa, bbb, ccc;
    MarketData aaaData, bbbData, cccData;
    for (auto* engine : {&aaa, &bbb, &ccc})
        engine->shareOrderManagement(&shared);
    aaa.setUp(aaaConfig, aaaFactory, aaaData, &broker);
    bbb.setUp(bbbConfig, bbbFactory, bbbData, &broker);
    ccc.setUp(cccConfig, cccFactory, cccData, &broker);
    EXPECT_EQ(aaa.getOms(), &shared);
    EXPECT_EQ(ccc.getOms(), &shared);

    // Bars interleave like the live stream: AAA dips, BBB dips, CCC stays quiet
    std::vector<MarketCondition> a = renamed(quietThenDip(), "AAA");
    std::vector<MarketCondition> b = renamed(quietThenDip(), "BBB");
    std::vector<MarketCondition> c = quiet("CCC");
    for (size_t i = 0; i < a.size(); i++)
    {
        aaaData.addBar(a[i]);
        aaa.run();
        bbbData.addBar(b[i]);
        bbb.run();
        if (i < c.size())
        {
            cccData.addBar(c[i]);
            ccc.run();
        }
    }

    // One order per dipping stock, each for its own ticker at its own price; none for the quiet one
    ASSERT_EQ(broker.orders.size(), 2u);
    EXPECT_EQ(broker.orders[0].getTicker(), "AAA");
    EXPECT_EQ(broker.orders[1].getTicker(), "BBB");
    EXPECT_FLOAT_EQ(broker.orders[0].getPrice(), 97.0f);
    ASSERT_EQ(shared.getOrders().size(), 2u);   // both in the one shared book
    EXPECT_NE(shared.getOrders()[0].getId(), shared.getOrders()[1].getId());
}

TEST(SharedOmsTests, EngineUsesItsOwnOmsByDefault)
{
    StrategyEngine one, two;
    EXPECT_NE(one.getOms(), nullptr);
    EXPECT_NE(one.getOms(), two.getOms());
    OrderManagement shared;
    one.shareOrderManagement(&shared);
    EXPECT_EQ(one.getOms(), &shared);
    one.shareOrderManagement(nullptr);   // back to its own
    EXPECT_NE(one.getOms(), &shared);
}
