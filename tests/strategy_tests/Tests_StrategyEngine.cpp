#include <gtest/gtest.h>
#include "../../src/strategy_engine/StrategyEngine.hpp"
#include "../../src/broker/SimulatedBroker.hpp"
#include "../../src/oms/OrderManagement.hpp"


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
    cut.setUp(jsonConfig, stratFactory, marketData);
}


// When it comes to testing strat engine correclty makes orders


// ---------- Strategies must agree before an order is sent ----------

TEST(StrategyConsensusTests, NoProposalsMeansNoOrder)
{
    EXPECT_FALSE(StrategyEngine::agreedOrder({}));
}

TEST(StrategyConsensusTests, SingleStrategyOrderIsSent)
{
    std::vector<Order> proposals = {Order(OrderType::BUY, "NVDA", 1, 100)};
    std::optional<Order> chosen = StrategyEngine::agreedOrder(proposals);
    ASSERT_TRUE(chosen);
    EXPECT_EQ(chosen->getTypeAsString(), "BUY");
}

TEST(StrategyConsensusTests, AgreeingStrategiesSendOneOrder)
{
    std::vector<Order> proposals = {Order(OrderType::SELL, "NVDA", 1, 100), Order(OrderType::SELL, "NVDA", 2, 101)};
    std::optional<Order> chosen = StrategyEngine::agreedOrder(proposals);
    ASSERT_TRUE(chosen);
    EXPECT_EQ(chosen->getTypeAsString(), "SELL");
    EXPECT_FLOAT_EQ(chosen->getQuantity(), 1); // the first strategy's order, sent once
}

TEST(StrategyConsensusTests, DisagreeingStrategiesSendNothing)
{
    std::vector<Order> proposals = {Order(OrderType::BUY, "NVDA", 1, 100), Order(OrderType::SELL, "NVDA", 1, 100)};
    EXPECT_FALSE(StrategyEngine::agreedOrder(proposals));
}

TEST(StrategyConsensusTests, LimitBuyAgreesWithMarketBuy)
{
    std::vector<Order> proposals = {Order(OrderType::BUY, "NVDA", 1, 100), Order(OrderType::LIMIT_BUY, "NVDA", 1, 99)};
    EXPECT_TRUE(StrategyEngine::agreedOrder(proposals));
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

    // One bar the way the live app and the tester handle it: the engine decides, the OMS sends
    void decideAndSend(StrategyEngine& engine, MarketData& data, const MarketCondition& bar, OrderManagement& oms)
    {
        data.addBar(bar);
        std::optional<Order> order = engine.decide(oms.heldQuantity(bar.Ticker));
        if (!order)
            return;
        oms.setMarketData(data);
        oms.sendOrder(*order);
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
    OrderManagement oms;
    oms.setUp(config, &broker);
    MarketData data;
    engine.setUp(config, factory, data);

    for (const auto& bar : quietThenDip())
        decideAndSend(engine, data, bar, oms);

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
        OrderManagement oms;
        oms.setUp(config, &broker);
        MarketData data;
        engine.setUp(config, factory, data);
        for (const auto& bar : quietThenDip())
            decideAndSend(engine, data, bar, oms);
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
    aaa.setUp(aaaConfig, aaaFactory, aaaData);
    bbb.setUp(bbbConfig, bbbFactory, bbbData);
    ccc.setUp(cccConfig, cccFactory, cccData);

    // Bars interleave like the live stream: AAA dips, BBB dips, CCC stays quiet
    std::vector<MarketCondition> a = renamed(quietThenDip(), "AAA");
    std::vector<MarketCondition> b = renamed(quietThenDip(), "BBB");
    std::vector<MarketCondition> c = quiet("CCC");
    for (size_t i = 0; i < a.size(); i++)
    {
        decideAndSend(aaa, aaaData, a[i], shared);
        decideAndSend(bbb, bbbData, b[i], shared);
        if (i < c.size())
            decideAndSend(ccc, cccData, c[i], shared);
    }

    // One order per dipping stock, each for its own ticker at its own price; none for the quiet one
    ASSERT_EQ(broker.orders.size(), 2u);
    EXPECT_EQ(broker.orders[0].getTicker(), "AAA");
    EXPECT_EQ(broker.orders[1].getTicker(), "BBB");
    EXPECT_FLOAT_EQ(broker.orders[0].getPrice(), 97.0f);
    ASSERT_EQ(shared.getOrders().size(), 2u);   // both in the one shared book
    EXPECT_NE(shared.getOrders()[0].getId(), shared.getOrders()[1].getId());
}

TEST(StrategyEngineDecisionTests, EngineOnlyDecides)
{
    // The engine returns the agreed order; nothing reaches a broker until the caller sends it
    json config = twoStrategies();
    StrategyFactory factory(config);
    StrategyEngine engine;
    MarketData data;
    engine.setUp(config, factory, data);

    std::optional<Order> last;
    for (const auto& bar : quietThenDip())
    {
        data.addBar(bar);
        last = engine.decide(0);
    }
    ASSERT_TRUE(last);
    EXPECT_EQ(last->getTypeAsString(), "BUY");
    EXPECT_EQ(last->getStrategy(), "RSI+MEANREV");
}
