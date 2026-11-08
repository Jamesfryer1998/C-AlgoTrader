#include <gtest/gtest.h>
#include "FakeAlpacaClient.hpp"
#include "../../src/alpaca/AlpacaApi.hpp"
#include "../../src/alpaca/AlpacaBroker.hpp"

/**
 * AlpacaBroker against a fake Alpaca: what it sends for each order, and how it reads Alpaca's replies.
 */
namespace
{
    const std::string PAPER = AlpacaApi::PAPER_TRADING_URL;
    const std::string ORDERS = AlpacaApi::ORDERS_PATH;

    json paperConfig(bool dryRun = false)
    {
        return {{"alpaca", {{"trading_url", PAPER}, {"data_feed", "iex"}, {"dry_run", dryRun}}}};
    }

    struct AlpacaBrokerRequestTests : public ::testing::Test
    {
        std::shared_ptr<FakeAlpacaClient> alpaca = std::make_shared<FakeAlpacaClient>();

        // An entry with a 5% stop loss and 10% take profit: becomes a bracket order
        static Order buyWithBracket()
        {
            Order order(OrderType::BUY, "KO", 10, 60.0f);
            order.setStopLoss(5);
            order.setTakeProfit(10);
            order.setClientOrderId("algo-7");
            return order;
        }

        // Sells some of a long position of `held`
        static Order sellPartOf(float held, float quantity)
        {
            Order order(OrderType::SELL, "KO", quantity, 60.0f);
            order.setStopLoss(5);
            order.setTakeProfit(10);
            order.setPositionBefore(held);
            return order;
        }
    };
}

// ---------- Connecting ----------

TEST_F(AlpacaBrokerRequestTests, ConnectReadsTheAccount)
{
    alpaca->reply("GET", AlpacaApi::ACCOUNT_PATH, 200,
                  {{"status", "ACTIVE"}, {"cash", "98150.70"}, {"buying_power", "396385.69"}});
    AlpacaBroker broker(paperConfig(), alpaca);

    EXPECT_EQ(broker.connect(), 1);
    EXPECT_EQ(broker.getConnectedAccount().status, "ACTIVE");
    EXPECT_DOUBLE_EQ(broker.getConnectedAccount().cash, 98150.70);
    EXPECT_DOUBLE_EQ(broker.getConnectedAccount().buyingPower, 396385.69);
    EXPECT_TRUE(broker.isPaper());
    ASSERT_EQ(alpaca->sent("GET").size(), 1u);
    EXPECT_EQ(alpaca->sent("GET")[0].url, PAPER + AlpacaApi::ACCOUNT_PATH);
}

TEST_F(AlpacaBrokerRequestTests, ConnectFailsWithoutKeysOrAnAccount)
{
    auto noKeys = std::make_shared<FakeAlpacaClient>(false);
    AlpacaBroker withoutKeys(paperConfig(), noKeys);
    EXPECT_EQ(withoutKeys.connect(), 0);
    EXPECT_TRUE(noKeys->requests.empty());   // doesn't even try

    alpaca->reply("GET", AlpacaApi::ACCOUNT_PATH, 401, {{"message", "unauthorized"}});
    AlpacaBroker unauthorised(paperConfig(), alpaca);
    EXPECT_EQ(unauthorised.connect(), 0);
}

TEST_F(AlpacaBrokerRequestTests, RealMoneyIsNotPaper)
{
    json live = {{"alpaca", {{"trading_url", "https://api.alpaca.markets"}}}};
    EXPECT_FALSE(AlpacaBroker(live, alpaca).isPaper());
}

// ---------- Entries ----------

TEST_F(AlpacaBrokerRequestTests, AnEntryIsPostedAsABracketOrder)
{
    alpaca->reply("POST", ORDERS, 200, {{"id", "alpaca-1"}, {"status", "accepted"}});
    AlpacaBroker broker(paperConfig(), alpaca);

    EXPECT_EQ(broker.placeOrder(buyWithBracket()), 1);

    std::vector<FakeAlpacaClient::Request> posted = alpaca->sent("POST", ORDERS);
    ASSERT_EQ(posted.size(), 1u);
    EXPECT_EQ(posted[0].url, PAPER + ORDERS);
    const json& body = posted[0].body;
    EXPECT_EQ(body["symbol"], "KO");
    EXPECT_EQ(body["qty"], "10");
    EXPECT_EQ(body["side"], "buy");
    EXPECT_EQ(body["type"], "market");
    EXPECT_EQ(body["order_class"], "bracket");
    EXPECT_EQ(body["client_order_id"], "algo-7");
    EXPECT_EQ(body["stop_loss"]["stop_price"], "57.00");
    EXPECT_EQ(body["take_profit"]["limit_price"], "66.00");

    EXPECT_EQ(broker.lastReceipt.brokerOrderId, "alpaca-1");
    EXPECT_EQ(broker.lastReceipt.brokerStatus, "accepted");
    EXPECT_EQ(broker.lastReceipt.orderClass, "bracket");
}

TEST_F(AlpacaBrokerRequestTests, ARejectedOrderSaysWhy)
{
    alpaca->reply("POST", ORDERS, 403, {{"message", "insufficient buying power"}});
    AlpacaBroker broker(paperConfig(), alpaca);

    EXPECT_EQ(broker.placeOrder(buyWithBracket()), 0);
    EXPECT_EQ(broker.lastReceipt.brokerStatus, BrokerBase::REJECTED);
    EXPECT_NE(broker.lastReceipt.error.find("insufficient buying power"), std::string::npos);
    EXPECT_NE(broker.lastReceipt.error.find("403"), std::string::npos);
}

TEST_F(AlpacaBrokerRequestTests, ADryRunSendsNothing)
{
    AlpacaBroker broker(paperConfig(true), alpaca);
    EXPECT_EQ(broker.placeOrder(buyWithBracket()), 1);
    EXPECT_EQ(broker.lastReceipt.brokerStatus, BrokerBase::DRY_RUN);
    EXPECT_TRUE(alpaca->requests.empty());
    EXPECT_FALSE(broker.reportsRealPositions());
}

TEST_F(AlpacaBrokerRequestTests, TheTestFeedIsAlwaysADryRun)
{
    json testFeed = {{"alpaca", {{"data_feed", "test"}}}};
    AlpacaBroker broker(testFeed, alpaca);
    EXPECT_TRUE(broker.isDryRun());
}

// ---------- Exits ----------

TEST_F(AlpacaBrokerRequestTests, AnExitCancelsTheOpenStopsFirst)
{
    // Two open orders (the bracket's stop and target), gone once cancelled
    alpaca->reply("GET", ORDERS + "?status=open", 200, json::array({{{"id", "stop-1"}}, {{"id", "target-1"}}}));
    alpaca->reply("GET", ORDERS + "?status=open", 200, json::array());
    alpaca->reply("POST", ORDERS, 200, {{"id", "exit-1"}, {"status", "accepted"}});
    AlpacaBroker broker(paperConfig(), alpaca);

    EXPECT_EQ(broker.placeOrder(sellPartOf(10, 10)), 1);

    std::vector<FakeAlpacaClient::Request> cancelled = alpaca->sent("DELETE");
    ASSERT_EQ(cancelled.size(), 2u);
    EXPECT_EQ(cancelled[0].url, PAPER + ORDERS + "/stop-1");
    EXPECT_EQ(cancelled[1].url, PAPER + ORDERS + "/target-1");

    // A plain order (Alpaca refuses brackets that reduce a position), sent after the cancels
    std::vector<FakeAlpacaClient::Request> posted = alpaca->sent("POST", ORDERS);
    ASSERT_EQ(posted.size(), 1u);   // closed in full: nothing left to protect
    EXPECT_EQ(posted[0].body["side"], "sell");
    EXPECT_FALSE(posted[0].body.contains("order_class"));
    EXPECT_EQ(broker.lastReceipt.brokerOrderId, "exit-1");
}

TEST_F(AlpacaBrokerRequestTests, APartialExitProtectsWhatIsLeft)
{
    alpaca->reply("GET", ORDERS + "?status=open", 200, json::array());
    alpaca->reply("POST", ORDERS, 200, {{"id", "exit-1"}, {"status", "accepted"}});
    AlpacaBroker broker(paperConfig(), alpaca);

    EXPECT_EQ(broker.placeOrder(sellPartOf(10, 4)), 1);

    std::vector<FakeAlpacaClient::Request> posted = alpaca->sent("POST", ORDERS);
    ASSERT_EQ(posted.size(), 2u);
    EXPECT_EQ(posted[0].body["qty"], "4");
    const json& protection = posted[1].body;
    EXPECT_EQ(protection["order_class"], "oco");
    EXPECT_EQ(protection["qty"], "6");
    EXPECT_EQ(protection["side"], "sell");
}

TEST_F(AlpacaBrokerRequestTests, AnExitIsTriedAgainWhileTheSharesAreStillReserved)
{
    alpaca->reply("GET", ORDERS + "?status=open", 200, json::array());
    alpaca->reply("POST", ORDERS, 403, {{"message", "insufficient qty available for order"}});
    alpaca->reply("POST", ORDERS, 200, {{"id", "exit-1"}, {"status", "accepted"}});
    AlpacaBroker broker(paperConfig(), alpaca);

    EXPECT_EQ(broker.placeOrder(sellPartOf(10, 10)), 1);
    EXPECT_EQ(alpaca->sent("POST", ORDERS).size(), 2u);
    EXPECT_EQ(alpaca->waits.size(), 1u);   // waited once (not slept, in the fake)
}

TEST_F(AlpacaBrokerRequestTests, AnExitGivesUpAfterThreeTries)
{
    alpaca->reply("GET", ORDERS + "?status=open", 200, json::array());
    alpaca->reply("POST", ORDERS, 403, {{"message", "insufficient qty available for order"}});
    AlpacaBroker broker(paperConfig(), alpaca);

    EXPECT_EQ(broker.placeOrder(sellPartOf(10, 10)), 0);
    EXPECT_EQ(alpaca->sent("POST", ORDERS).size(), 3u);
    EXPECT_EQ(broker.lastReceipt.brokerStatus, BrokerBase::REJECTED);
}

// ---------- Orders still filling ----------

TEST_F(AlpacaBrokerRequestTests, PendingUntilAlpacaSaysTheOrderFinished)
{
    alpaca->reply("POST", ORDERS, 200, {{"id", "alpaca-1"}, {"status", "accepted"}});
    alpaca->reply("GET", ORDERS + "/alpaca-1", 200, {{"status", "new"}});
    alpaca->reply("GET", ORDERS + "/alpaca-1", 200, {{"status", "filled"}});
    AlpacaBroker broker(paperConfig(), alpaca);

    EXPECT_FALSE(broker.hasPendingOrder("KO"));   // nothing sent yet
    broker.placeOrder(buyWithBracket());
    EXPECT_TRUE(broker.hasPendingOrder("KO"));    // "new"
    EXPECT_FALSE(broker.hasPendingOrder("KO"));   // "filled"
    size_t asked = alpaca->sent("GET", ORDERS + "/alpaca-1").size();
    EXPECT_FALSE(broker.hasPendingOrder("KO"));   // settled: not asked again
    EXPECT_EQ(alpaca->sent("GET", ORDERS + "/alpaca-1").size(), asked);
}

TEST_F(AlpacaBrokerRequestTests, PendingWhenAlpacaCantBeAsked)
{
    alpaca->reply("POST", ORDERS, 200, {{"id", "alpaca-1"}, {"status", "accepted"}});
    alpaca->reply("GET", ORDERS + "/alpaca-1", 503, {{"message", "unavailable"}});
    AlpacaBroker broker(paperConfig(), alpaca);
    broker.placeOrder(buyWithBracket());
    EXPECT_TRUE(broker.hasPendingOrder("KO"));   // safer to wait than to correct a position about to change
}

// ---------- Positions, prices, the clock and the account ----------

TEST_F(AlpacaBrokerRequestTests, PositionsLongShortAndFlat)
{
    alpaca->reply("GET", AlpacaApi::POSITIONS_PATH + "/KO", 200,
                  {{"qty", "10"}, {"side", "long"}, {"avg_entry_price", "60.25"}});
    alpaca->reply("GET", AlpacaApi::POSITIONS_PATH + "/PEP", 200,
                  {{"qty", "-3"}, {"side", "short"}, {"avg_entry_price", "150.10"}});
    alpaca->reply("GET", AlpacaApi::POSITIONS_PATH + "/WMT", FakeAlpacaClient::NOT_FOUND,
                  {{"message", "position does not exist"}});
    AlpacaBroker broker(paperConfig(), alpaca);

    Position ko = broker.getLatestPosition("KO");
    EXPECT_FLOAT_EQ(ko.getQuantity(), 10);
    EXPECT_FLOAT_EQ(ko.getAvgPrice(), 60.25f);
    EXPECT_EQ(broker.lastPositionDetails()["avg_entry_price"], "60.25");

    EXPECT_FLOAT_EQ(broker.getLatestPosition("PEP").getQuantity(), -3);

    Position wmt = broker.getLatestPosition("WMT");   // Alpaca's 404 = we hold none
    EXPECT_FLOAT_EQ(wmt.getQuantity(), 0);
    EXPECT_TRUE(broker.lastPositionReliable);
    EXPECT_TRUE(broker.lastPositionDetails().empty());
}

TEST_F(AlpacaBrokerRequestTests, AFailedPositionRequestIsNotTrusted)
{
    alpaca->reply("GET", AlpacaApi::POSITIONS_PATH + "/KO", 500, {{"message", "internal error"}});
    AlpacaBroker broker(paperConfig(), alpaca);
    EXPECT_FLOAT_EQ(broker.getLatestPosition("KO").getQuantity(), 0);
    EXPECT_FALSE(broker.lastPositionReliable);   // not "we hold nothing"
}

TEST_F(AlpacaBrokerRequestTests, LatestPrice)
{
    alpaca->reply("GET", "/v2/stocks/KO/trades/latest", 200, {{"trade", {{"p", 60.42}}}});
    AlpacaBroker broker(paperConfig(), alpaca);
    EXPECT_FLOAT_EQ(broker.getLatestPrice("KO"), 60.42f);
    EXPECT_NE(alpaca->requests[0].url.find("feed=iex"), std::string::npos);
    EXPECT_FLOAT_EQ(broker.getLatestPrice("PEP"), 0);   // no reply: 0, with a warning
}

TEST_F(AlpacaBrokerRequestTests, MarketClock)
{
    alpaca->reply("GET", AlpacaApi::CLOCK_PATH, 200,
                  {{"is_open", false}, {"next_open", "2026-11-09T09:30:00-05:00"},
                   {"next_close", "2026-11-09T16:00:00-05:00"}});
    AlpacaBroker broker(paperConfig(), alpaca);
    AlpacaBroker::MarketClock clock = broker.getMarketClock();
    EXPECT_TRUE(clock.known);
    EXPECT_FALSE(clock.isOpen);
    EXPECT_EQ(clock.nextClose - clock.nextOpen, 6 * 3600 + 30 * 60);

    auto down = std::make_shared<FakeAlpacaClient>();
    EXPECT_FALSE(AlpacaBroker(paperConfig(), down).getMarketClock().known);
}

TEST_F(AlpacaBrokerRequestTests, AccountFigures)
{
    alpaca->reply("GET", AlpacaApi::ACCOUNT_PATH, 200,
                  {{"equity", "100250.50"}, {"last_equity", "100000"}, {"cash", "50000"}});
    AlpacaBroker broker(paperConfig(), alpaca);
    AccountSnapshot account = broker.getAccount();
    EXPECT_TRUE(account.available);
    EXPECT_DOUBLE_EQ(account.equity, 100250.50);
    EXPECT_DOUBLE_EQ(account.todayPnL(), 250.50);
    EXPECT_DOUBLE_EQ(account.cash, 50000);

    EXPECT_FALSE(AlpacaBroker(paperConfig(true), alpaca).getAccount().available);   // a dry run has none
}
