#include <gtest/gtest.h>
#include "alpaca/AlpacaBroker.hpp"
#include "alpaca/AlpacaStream.hpp"
#include "data_access/MarketData.hpp"

// ---------- Order JSON sent to Alpaca ----------

TEST(AlpacaBrokerTests, MarketBuyOrderJson)
{
    Order order(OrderType::BUY, "NVDA", 1, 181.234f);
    json body = AlpacaBroker::buildOrderJson(order);

    EXPECT_EQ(body["symbol"], "NVDA");
    EXPECT_EQ(body["qty"], "1");
    EXPECT_EQ(body["side"], "buy");
    EXPECT_EQ(body["type"], "market");
    EXPECT_EQ(body["time_in_force"], "day");
    EXPECT_FALSE(body.contains("order_class")); // no stop loss / take profit set
}

TEST(AlpacaBrokerTests, BracketOrderWhenStopLossAndTakeProfitSet)
{
    Order order(OrderType::BUY, "NVDA", 2, 100.0f);
    order.setStopLoss(10);    // 90.00
    order.setTakeProfit(10);  // 110.00

    json body = AlpacaBroker::buildOrderJson(order);

    EXPECT_EQ(body["order_class"], "bracket");
    EXPECT_EQ(body["stop_loss"]["stop_price"], "90.00");
    EXPECT_EQ(body["take_profit"]["limit_price"], "110.00");
}

TEST(AlpacaBrokerTests, SellBracketHasStopAboveAndTargetBelow)
{
    Order order(OrderType::SELL, "NVDA", 1, 200.0f);
    order.setStopLoss(5);     // 210.00
    order.setTakeProfit(5);   // 190.00

    json body = AlpacaBroker::buildOrderJson(order);

    EXPECT_EQ(body["side"], "sell");
    EXPECT_EQ(body["stop_loss"]["stop_price"], "210.00");
    EXPECT_EQ(body["take_profit"]["limit_price"], "190.00");
}

TEST(AlpacaBrokerTests, LimitOrderPriceRoundedToCents)
{
    Order order(OrderType::LIMIT_SELL, "NVDA", 3, 181.23789f);
    json body = AlpacaBroker::buildOrderJson(order);

    EXPECT_EQ(body["type"], "limit");
    EXPECT_EQ(body["side"], "sell");
    EXPECT_EQ(body["limit_price"], "181.24");
}

TEST(AlpacaBrokerTests, TestFeedIsAlwaysDryRun)
{
    json config = {{"ticker", "NVDA"}, {"alpaca", {{"data_feed", "test"}, {"dry_run", false}}}};
    AlpacaBroker broker(config);
    EXPECT_TRUE(broker.isDryRun());
}

// ---------- Bars received from Alpaca ----------

TEST(AlpacaStreamTests, ParseStreamBar)
{
    json bar = json::parse(R"({"T":"b","S":"NVDA","o":181.5,"h":182.0,"l":181.1,"c":181.9,"v":12345,
                               "t":"2026-10-28T14:31:00Z","n":120,"vw":181.6})");

    MarketCondition condition = AlpacaStream::parseBar(bar, "NVDA");

    EXPECT_EQ(condition.DateTime, "2026-10-28 14:31:00");
    EXPECT_EQ(condition.Ticker, "NVDA");
    EXPECT_FLOAT_EQ(condition.Open, 181.5f);
    EXPECT_FLOAT_EQ(condition.Close, 181.9f);
    EXPECT_EQ(condition.Volume, 12345);
    EXPECT_EQ(condition.TimeInterval, "1m");
    EXPECT_TRUE(condition.IsValid());
}

TEST(AlpacaStreamTests, ParseRestBarWithoutSymbolUsesOurSymbol)
{
    // Bars from the REST history endpoint don't include "S"
    json bar = json::parse(R"({"o":10,"c":11,"v":5,"t":"2026-10-27T19:59:00Z"})");

    MarketCondition condition = AlpacaStream::parseBar(bar, "NVDA");

    EXPECT_EQ(condition.Ticker, "NVDA");
    EXPECT_EQ(condition.DateTime, "2026-10-27 19:59:00");
}

TEST(AlpacaStreamTests, TestFeedUsesFakeSymbol)
{
    json config = {{"ticker", "NVDA"}, {"alpaca", {{"data_feed", "test"}}}};
    AlpacaStream stream(config);
    EXPECT_EQ(stream.getSymbol(), "FAKEPACA");
}

// ---------- Rolling window of live bars ----------

TEST(MarketDataLiveTests, AddBarKeepsOnlyNewestBars)
{
    MarketData marketData;
    for (int i = 0; i < 10; i++)
        marketData.addBar(MarketCondition("t" + std::to_string(i), "NVDA", i, i, 1, "1m"), 3);

    auto data = marketData.getData();
    ASSERT_EQ(data.size(), 3u);
    EXPECT_EQ(data.front().DateTime, "t7");
    EXPECT_EQ(data.back().DateTime, "t9");
}
