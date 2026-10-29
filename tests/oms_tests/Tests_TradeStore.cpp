#include <gtest/gtest.h>
#include <filesystem>
#include <unistd.h>
#include "../../src/oms/OrderManagement.hpp"
#include "../../src/oms/TradeStore.hpp"

namespace fs = std::filesystem;

// A broker we fully control: placing an order changes the position it reports
class FakeBroker : public BrokerBase
{
    public:
        float heldQuantity = 0;

        int connect() override { return 1; }
        int disconnect() override { return 1; }
        float getLatestPrice(std::string) override { return 100; }
        int placeOrder(Order order) override
        {
            heldQuantity += order.isBuy() ? order.getQuantity() : -order.getQuantity();
            return 1;
        }
        Position getLatestPosition(std::string ticker) override { return Position(ticker, heldQuantity, 100); }
};

class TradeStoreTests : public ::testing::Test
{
    protected:
        std::string dir;

        void SetUp() override
        {
            dir = (fs::temp_directory_path() / ("algo_trader_store_" + std::to_string(::getpid()))).string();
            fs::remove_all(dir);
        }
        void TearDown() override { fs::remove_all(dir); }

        static MarketData barsAt(float price)
        {
            MarketData data;
            std::vector<MarketCondition> bars = {MarketCondition("2026-10-29 10:00:00", "NVDA", price, price, 100, "1m")};
            data.update(bars);
            return data;
        }

        static json omsConfig()
        {
            return {{"max_position_size", 10}, {"max_exposure", 5.0}, {"slippage_tolerance", 2.0}};
        }

        static Order buy(float qty, float price)
        {
            Order order(OrderType::BUY, "NVDA", qty, price);
            order.setStopLoss(10);
            order.setTakeProfit(10);
            return order;
        }
};

TEST_F(TradeStoreTests, TradesRoundTrip)
{
    TradeStore store(dir);
    TradeRecord t{"2026-10-29 10:00:00", "NVDA", "BUY", 2, 213.5, 192.15, 234.85};
    store.appendTrade(t);
    store.appendTrade({"2026-10-29 10:05:00", "NVDA", "SELL", 1, 214.0, 0, 0});

    std::vector<TradeRecord> loaded = TradeStore(dir).loadTrades();
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].time, "2026-10-29 10:00:00");
    EXPECT_EQ(loaded[0].type, "BUY");
    EXPECT_TRUE(loaded[0].isBuy());
    EXPECT_DOUBLE_EQ(loaded[0].quantity, 2);
    EXPECT_NEAR(loaded[0].price, 213.5, 1e-4);
    EXPECT_FALSE(loaded[1].isBuy());
}

TEST_F(TradeStoreTests, PositionsAreRewrittenNotAppended)
{
    TradeStore store(dir);
    store.savePositions({Position("NVDA", 3, 200)});
    store.savePositions({Position("NVDA", 5, 210)});

    std::vector<Position> loaded = store.loadPositions();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_FLOAT_EQ(loaded[0].getQuantity(), 5);
    EXPECT_FLOAT_EQ(loaded[0].getAvgPrice(), 210);
    EXPECT_FALSE(fs::exists(fs::path(dir) / "positions.csv.tmp"));
}

TEST_F(TradeStoreTests, MissingFilesLoadAsEmpty)
{
    TradeStore store(dir);
    EXPECT_TRUE(store.loadTrades().empty());
    EXPECT_TRUE(store.loadPositions().empty());
}

TEST_F(TradeStoreTests, OmsSavesTradesAndRestoresThemAfterRestart)
{
    MarketData data = barsAt(100);
    FakeBroker broker;
    {
        TradeStore store(dir);
        PerformanceTracker tracker(10000);
        OrderManagement oms;
        oms.setUp(omsConfig(), &broker);
        oms.setMarketData(data);
        oms.enablePersistence(&store, &tracker);

        Order order = buy(2, 100);
        oms.onNewOrder(order);
        EXPECT_DOUBLE_EQ(tracker.getPositionQuantity("NVDA"), 2);
    }

    // "Restart": fresh OMS and tracker, same folder
    TradeStore store(dir);
    PerformanceTracker tracker(10000);
    OrderManagement oms;
    oms.setUp(omsConfig(), &broker);
    oms.setMarketData(data);
    oms.enablePersistence(&store, &tracker);

    EXPECT_EQ(oms.getOrders().size(), 1u);
    ASSERT_EQ(oms.getPositions().size(), 1u);
    EXPECT_FLOAT_EQ(oms.getPositions()[0].getQuantity(), 2);
    EXPECT_DOUBLE_EQ(tracker.getPositionQuantity("NVDA"), 2);
    EXPECT_EQ(oms.latestOrderId, 2); // the next order continues numbering
}

TEST_F(TradeStoreTests, BrokerSideExitIsRecordedAsAnAdjustment)
{
    MarketData data = barsAt(100);
    FakeBroker broker;
    TradeStore store(dir);
    PerformanceTracker tracker(10000);
    OrderManagement oms;
    oms.setUp(omsConfig(), &broker);
    oms.setMarketData(data);
    oms.enablePersistence(&store, &tracker);

    Order order = buy(2, 100);
    oms.onNewOrder(order);

    // The broker's take profit sells everything without us sending an order
    broker.heldQuantity = 0;
    MarketData later = barsAt(110);
    oms.setMarketData(later);
    oms.syncPosition("NVDA");

    EXPECT_DOUBLE_EQ(tracker.getPositionQuantity("NVDA"), 0);
    EXPECT_NEAR(tracker.summary().realisedPnL, 20.0, 1e-6); // 2 x (110 - 100)
    ASSERT_EQ(store.loadTrades().size(), 2u);
    EXPECT_EQ(store.loadTrades()[1].type, "SELL");
    EXPECT_TRUE(oms.getPositions().empty());
}
