#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "../../src/oms/OrderManagement.hpp"
#include "../../src/oms/TradeStore.hpp"

namespace fs = std::filesystem;

// A broker we fully control: placing an order changes the position it reports
class FakeBroker : public BrokerBase
{
    public:
        float heldQuantity = 0;
        bool rejectNext = false;
        int ordersSeen = 0;
        std::string lastClientOrderId;

        int connect() override { return 1; }
        int disconnect() override { return 1; }
        float getLatestPrice(std::string) override { return 100; }
        int placeOrder(Order order) override
        {
            ordersSeen++;
            lastClientOrderId = order.getClientOrderId();
            lastReceipt = OrderReceipt();
            lastReceipt.orderClass = "bracket";
            lastReceipt.roundTripMs = 12.5;
            if (rejectNext)
            {
                rejectNext = false;
                lastReceipt.brokerStatus = "rejected";
                lastReceipt.error = "HTTP 403: insufficient qty available for order";
                return 0;
            }
            lastReceipt.brokerOrderId = "alpaca-" + std::to_string(ordersSeen);
            lastReceipt.brokerStatus = "accepted";
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

        static json omsConfig(int maxPosition = 10)
        {
            return {{"max_position_size", maxPosition}, {"max_exposure", 5.0}, {"slippage_tolerance", 2.0}};
        }

        static Order order(OrderType type, float qty, float price)
        {
            Order o(type, "NVDA", qty, price);
            o.setStopLoss(10);
            o.setTakeProfit(10);
            o.setStrategy("RSI");
            return o;
        }
};

TEST_F(TradeStoreTests, TradesRoundTripWithAllColumns)
{
    TradeRecord t;
    t.ourId = 7; t.time = "2026-10-29 10:00:00"; t.sentAtUtc = "2026-10-29T14:00:00.123Z"; t.strategy = "RSI";
    t.ticker = "NVDA"; t.side = "buy"; t.type = "BUY"; t.orderClass = "bracket"; t.quantity = 2; t.price = 213.5;
    t.stopLoss = 192.15; t.takeProfit = 234.85; t.status = "sent"; t.brokerStatus = "accepted";
    t.clientOrderId = "algo-7-1793300000"; t.brokerOrderId = "b1f2"; t.roundTripMs = 143.2;
    t.note = "has, commas\nand newlines";
    {
        TradeStore store(dir);
        store.appendTrade(t);
    } // destructor finishes the queued write

    std::vector<TradeRecord> loaded = TradeStore(dir).loadTrades();
    ASSERT_EQ(loaded.size(), 1u);
    const TradeRecord& r = loaded[0];
    EXPECT_EQ(r.ourId, 7);
    EXPECT_EQ(r.sentAtUtc, "2026-10-29T14:00:00.123Z");
    EXPECT_EQ(r.strategy, "RSI");
    EXPECT_EQ(r.side, "buy");
    EXPECT_EQ(r.orderClass, "bracket");
    EXPECT_NEAR(r.price, 213.5, 1e-4);
    EXPECT_EQ(r.status, "sent");
    EXPECT_EQ(r.brokerStatus, "accepted");
    EXPECT_EQ(r.clientOrderId, "algo-7-1793300000");
    EXPECT_EQ(r.brokerOrderId, "b1f2");
    EXPECT_NEAR(r.roundTripMs, 143.2, 0.05);
    EXPECT_EQ(r.note, "has; commas;and newlines");   // can't break the CSV
}

TEST_F(TradeStoreTests, WritesHappenInTheBackgroundAndFlushWaits)
{
    TradeStore store(dir);
    for (int i = 1; i <= 200; i++)
    {
        TradeRecord t;
        t.ourId = i; t.ticker = "NVDA"; t.side = "buy"; t.type = "BUY"; t.quantity = 1; t.price = 100; t.status = "sent";
        store.appendTrade(t);
    }
    store.flush();
    std::vector<TradeRecord> loaded = store.loadTrades();
    ASSERT_EQ(loaded.size(), 200u);
    EXPECT_EQ(loaded.front().ourId, 1);
    EXPECT_EQ(loaded.back().ourId, 200);   // order preserved
}

TEST_F(TradeStoreTests, PositionsAreRewrittenNotAppended)
{
    TradeStore store(dir);
    store.savePositions({Position("NVDA", 3, 200)});
    store.savePositions({Position("NVDA", 5, 210)});
    store.flush();

    std::vector<Position> loaded = store.loadPositions();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_FLOAT_EQ(loaded[0].getQuantity(), 5);
    EXPECT_FALSE(fs::exists(fs::path(dir) / "positions.csv.tmp"));
}

TEST_F(TradeStoreTests, OldFormatFileIsUpgradedKeepingTrades)
{
    fs::create_directories(dir);
    std::ofstream(fs::path(dir) / "trades.csv")
        << "time,ticker,type,quantity,price,stop_loss,take_profit\n"
        << "2026-10-29 14:21:00,NVDA,BUY,11.0000,212.9200,0.0000,0.0000\n";

    TradeStore store(dir);
    std::vector<TradeRecord> loaded = store.loadTrades();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].ourId, 1);
    EXPECT_EQ(loaded[0].side, "buy");
    EXPECT_EQ(loaded[0].status, "adjustment");   // no stop loss/take profit = it was a broker adjustment
    EXPECT_DOUBLE_EQ(loaded[0].quantity, 11);
    EXPECT_TRUE(fs::exists(fs::path(dir) / "trades.csv.old"));
}

TEST_F(TradeStoreTests, EveryAttemptIsRecordedSentRejectedAndBlocked)
{
    MarketData data = barsAt(100);
    FakeBroker broker;
    TradeStore store(dir);
    PerformanceTracker tracker(10000);
    OrderManagement oms;
    oms.setUp(omsConfig(3), &broker);
    oms.setMarketData(data);
    oms.enablePersistence(&store, &tracker);

    Order first = order(OrderType::BUY, 2, 100);
    oms.sendOrder(first);                       // sent
    broker.rejectNext = true;
    Order second = order(OrderType::SELL, 1, 100);
    oms.sendOrder(second);                      // rejected by the broker
    Order third = order(OrderType::BUY, 2, 100);
    oms.sendOrder(third);                       // blocked: 2 held + 2 > max 3
    store.flush();

    std::vector<TradeRecord> trades = store.loadTrades();
    ASSERT_EQ(trades.size(), 3u);

    EXPECT_EQ(trades[0].ourId, 1);
    EXPECT_EQ(trades[0].status, "sent");
    EXPECT_EQ(trades[0].strategy, "RSI");
    EXPECT_EQ(trades[0].brokerOrderId, "alpaca-1");
    EXPECT_EQ(trades[0].brokerStatus, "accepted");
    EXPECT_EQ(trades[0].orderClass, "bracket");
    EXPECT_EQ(trades[0].clientOrderId.rfind("algo-1-", 0), 0u);   // our id, sent to the broker
    EXPECT_FALSE(trades[0].sentAtUtc.empty());
    EXPECT_NEAR(trades[0].roundTripMs, 12.5, 0.05);

    EXPECT_EQ(trades[1].ourId, 2);
    EXPECT_EQ(trades[1].status, "rejected");
    EXPECT_NE(trades[1].note.find("insufficient qty"), std::string::npos);

    EXPECT_EQ(trades[2].ourId, 3);
    EXPECT_EQ(trades[2].status, "blocked");
    EXPECT_NE(trades[2].note.find("max_position_size"), std::string::npos);
    EXPECT_EQ(broker.ordersSeen, 2);   // the blocked one never reached the broker

    // Only the sent order changed our book
    EXPECT_DOUBLE_EQ(tracker.getPositionQuantity("NVDA"), 2);
}

TEST_F(TradeStoreTests, RestartContinuesIdsAndPositions)
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
        Order o = order(OrderType::BUY, 2, 100);
        oms.sendOrder(o);
        broker.rejectNext = true;
        Order r = order(OrderType::BUY, 1, 100);
        oms.sendOrder(r);
    }

    TradeStore store(dir);
    PerformanceTracker tracker(10000);
    OrderManagement oms;
    oms.setUp(omsConfig(), &broker);
    oms.setMarketData(data);
    oms.enablePersistence(&store, &tracker);

    EXPECT_EQ(oms.latestOrderId, 3);                         // ids 1 and 2 used, next is 3
    EXPECT_DOUBLE_EQ(tracker.getPositionQuantity("NVDA"), 2); // rejected order not replayed
    ASSERT_EQ(oms.getPositions().size(), 1u);
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

    Order o = order(OrderType::BUY, 2, 100);
    oms.sendOrder(o);

    // The broker's take profit sells everything without us sending an order
    broker.heldQuantity = 0;
    MarketData later = barsAt(110);
    oms.setMarketData(later);
    oms.syncPosition("NVDA");
    store.flush();

    EXPECT_DOUBLE_EQ(tracker.getPositionQuantity("NVDA"), 0);
    EXPECT_NEAR(tracker.summary().realisedPnL, 20.0, 1e-6);   // 2 x (110 - 100)
    std::vector<TradeRecord> trades = store.loadTrades();
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[1].status, "adjustment");
    EXPECT_EQ(trades[1].side, "sell");
    EXPECT_EQ(trades[1].ourId, 2);
    EXPECT_TRUE(oms.getPositions().empty());
}

TEST_F(TradeStoreTests, AdoptingAPositionUsesTheBrokersAverageCost)
{
    // Broker already holds 11 shares bought at $100 (FakeBroker's average) before we kept records;
    // the latest bar closed at $110. Our book should take them on at $100, not $110.
    MarketData data = barsAt(110);
    FakeBroker broker;
    broker.heldQuantity = 11;
    TradeStore store(dir);
    PerformanceTracker tracker(10000);
    OrderManagement oms;
    oms.setUp(omsConfig(20), &broker);
    oms.setMarketData(data);
    oms.enablePersistence(&store, &tracker);

    oms.syncPosition("NVDA");
    store.flush();

    tracker.markPrice("NVDA", 110);
    EXPECT_DOUBLE_EQ(tracker.getPositionQuantity("NVDA"), 11);
    EXPECT_NEAR(tracker.summary().unrealisedPnL, 110.0, 1e-6);   // 11 x (110 - 100), same as the broker
    std::vector<TradeRecord> trades = store.loadTrades();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].status, "adjustment");
    EXPECT_NEAR(trades[0].price, 100.0, 1e-6);
}

TEST_F(TradeStoreTests, FailedPositionRequestIsNotMistakenForFlat)
{
    MarketData data = barsAt(100);
    FakeBroker broker;
    TradeStore store(dir);
    PerformanceTracker tracker(10000);
    OrderManagement oms;
    oms.setUp(omsConfig(), &broker);
    oms.setMarketData(data);
    oms.enablePersistence(&store, &tracker);

    Order o = order(OrderType::BUY, 2, 100);
    oms.sendOrder(o);
    store.flush();
    size_t tradesBefore = store.loadTrades().size();

    // A network blip: the broker answers "0" but flags it as unreliable
    class FailingBroker : public FakeBroker
    {
        public:
            Position getLatestPosition(std::string ticker) override
            {
                lastPositionReliable = false;
                return Position(ticker, 0, 0);
            }
    } failing;
    oms.setUp(omsConfig(), &failing);
    oms.syncPosition("NVDA");
    store.flush();

    EXPECT_DOUBLE_EQ(tracker.getPositionQuantity("NVDA"), 2);   // unchanged
    EXPECT_EQ(store.loadTrades().size(), tradesBefore);           // no fake "sell" recorded
    ASSERT_EQ(oms.getPositions().size(), 1u);
}

// Alpaca fills a market order a moment after accepting it. Until then its position doesn't include the
// order, and our records must not be "corrected" to match (that logged a fake opposite trade, then
// another undoing it a minute later).
namespace
{
    class SlowFillBroker : public FakeBroker
    {
        public:
            bool pending = false;
            float filled = 0;   // what the broker reports until the order fills
            float lastPositionBefore = 99;
            int placeOrder(Order order) override
            {
                lastPositionBefore = order.getPositionBefore();
                float before = heldQuantity;
                int ok = FakeBroker::placeOrder(order);
                filled = before;   // not filled yet
                pending = ok != 0;
                return ok;
            }
            Position getLatestPosition(std::string ticker) override
            {
                return Position(ticker, pending ? filled : heldQuantity, 100);
            }
            bool hasPendingOrder(const std::string&) override { return pending; }
    };
}

TEST_F(TradeStoreTests, NoFakeAdjustmentWhileAnOrderIsFilling)
{
    TradeStore store(dir);
    PerformanceTracker tracker(100000);
    SlowFillBroker broker;
    OrderManagement oms;
    MarketData data = barsAt(100);
    oms.setUp(omsConfig(), &broker);
    oms.setMarketData(data);
    oms.enablePersistence(&store, &tracker);

    Order buy = order(OrderType::BUY, 1, 100);
    oms.sendOrder(buy);
    EXPECT_FLOAT_EQ(broker.lastPositionBefore, 0);   // the broker was told we held nothing

    // Still filling: our book already counts it, and nothing was "adjusted"
    oms.syncPosition("NVDA");
    ASSERT_EQ(oms.getPositions().size(), 1u);
    EXPECT_FLOAT_EQ(oms.getPositions()[0].getQuantity(), 1);
    EXPECT_DOUBLE_EQ(tracker.getPositionQuantity("NVDA"), 1);

    // Filled: the broker now agrees, still no adjustment
    broker.pending = false;
    oms.syncPosition("NVDA");
    store.flush();
    std::vector<TradeRecord> trades = store.loadTrades();
    for (const auto& t : trades)
        EXPECT_NE(t.status, "adjustment") << t.note;
    EXPECT_FLOAT_EQ(oms.getPositions()[0].getQuantity(), 1);

    // The next order sees the position, so a SELL is an exit
    Order sell = order(OrderType::SELL, 1, 101);
    oms.sendOrder(sell);
    EXPECT_FLOAT_EQ(broker.lastPositionBefore, 1);
    EXPECT_TRUE(oms.getPositions().empty());   // closed in our book straight away
}

// A dry run, as AlpacaBroker does it: orders are accepted but never sent, so it always reports holding nothing
class DryRunBroker : public FakeBroker
{
    public:
        bool reportsRealPositions() const override { return false; }
        Position getLatestPosition(std::string ticker) override { return Position(ticker, 0, 0); }
};

TEST_F(TradeStoreTests, ADryRunKeepsItsOwnPositions)
{
    // Our book must not be "corrected" to the dry run's nothing: the strategies, the risk engine and
    // max_position_size all go by it
    DryRunBroker broker;
    OrderManagement oms;
    oms.setUp(omsConfig(10), &broker);
    MarketData data = barsAt(100);
    oms.setMarketData(data);

    Order first = order(OrderType::BUY, 5, 100), second = order(OrderType::BUY, 5, 100), third = order(OrderType::BUY, 5, 100);
    oms.sendOrder(first);
    EXPECT_FLOAT_EQ(oms.heldQuantity("NVDA"), 5);
    oms.sendOrder(second);
    EXPECT_FLOAT_EQ(oms.heldQuantity("NVDA"), 10);
    oms.sendOrder(third);                              // 15 would be over max_position_size
    EXPECT_FLOAT_EQ(oms.heldQuantity("NVDA"), 10);
    EXPECT_EQ(broker.ordersSeen, 2);
}

TEST_F(TradeStoreTests, OurBookAveragesAddsAndStartsAfreshWhenFlipping)
{
    DryRunBroker broker;
    OrderManagement oms;
    oms.setUp(omsConfig(), &broker);

    MarketData at100 = barsAt(100), at110 = barsAt(110), at90 = barsAt(90);
    oms.setMarketData(at100);
    Order first = order(OrderType::BUY, 2, 100);
    oms.sendOrder(first);
    oms.setMarketData(at110);
    Order more = order(OrderType::BUY, 2, 110);
    oms.sendOrder(more);
    ASSERT_EQ(oms.getPositions().size(), 1u);
    EXPECT_FLOAT_EQ(oms.getPositions()[0].getQuantity(), 4);
    EXPECT_FLOAT_EQ(oms.getPositions()[0].getAvgPrice(), 105);   // (2 x 100 + 2 x 110) / 4

    // Sell 6 of 4: now short 2, a new position at the sale price
    oms.setMarketData(at90);
    Order flip = order(OrderType::SELL, 6, 90);
    oms.sendOrder(flip);
    ASSERT_EQ(oms.getPositions().size(), 1u);
    EXPECT_FLOAT_EQ(oms.getPositions()[0].getQuantity(), -2);
    EXPECT_FLOAT_EQ(oms.getPositions()[0].getAvgPrice(), 90);

    // Buying 2 back closes it: no position left
    Order close = order(OrderType::BUY, 2, 90);
    oms.sendOrder(close);
    EXPECT_TRUE(oms.getPositions().empty());
}

TEST_F(TradeStoreTests, BadLinesAreSkippedNotFatal)
{
    fs::create_directories(dir);
    {
        TradeStore store(dir);
        TradeRecord good;
        good.ourId = 1;
        good.ticker = "NVDA";
        good.side = "buy";
        good.type = "BUY";
        good.quantity = 2;
        good.price = 100;
        good.status = "sent";
        store.appendTrade(good);
        store.flush();
    }
    std::ofstream(dir + "/trades.csv", std::ios::app) << "this,is,not,a,trade\n" << "x,2026-10-26 10:00:00,,RSI,NVDA,buy,BUY,simple,lots,100,0,0,sent,,,,0,\n";
    std::ofstream(dir + "/positions.csv") << "ticker,quantity,avg_price\nNVDA,2,100\nNVDA,lots,100\nshort line\n";

    TradeStore store(dir);
    std::vector<TradeRecord> trades = store.loadTrades();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].ourId, 1);
    std::vector<Position> positions = store.loadPositions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_FLOAT_EQ(positions[0].getQuantity(), 2);
}
