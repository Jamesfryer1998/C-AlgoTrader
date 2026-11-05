#include <gtest/gtest.h>
#include "alpaca/AlpacaDailyBars.hpp"
#include "alpaca/AlpacaBroker.hpp"
#include "alpaca/AlpacaApi.hpp"
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

TEST(AlpacaBrokerTests, BracketEntriesAreGoodTillCancelled)
{
    // A "day" bracket's stop loss expires at the close, leaving an overnight position unprotected
    Order order(OrderType::BUY, "NVDA", 1, 100.0f);
    order.setStopLoss(5);
    order.setTakeProfit(5);
    EXPECT_EQ(AlpacaBroker::buildOrderJson(order)["time_in_force"], "gtc");
}

TEST(AlpacaBrokerTests, ExitsArePlainOrdersNotBrackets)
{
    // Holding 2 short: a BUY reduces the position. Alpaca rejects brackets for that
    // ("bracket orders must be entry orders"), so it goes as a plain market order.
    Order exit(OrderType::BUY, "MDT", 1, 105.48f);
    exit.setStopLoss(5);
    exit.setTakeProfit(1);
    exit.setPositionBefore(-2);
    ASSERT_TRUE(exit.reducesPosition());

    json body = AlpacaBroker::buildOrderJson(exit);
    EXPECT_FALSE(body.contains("order_class"));
    EXPECT_FALSE(body.contains("stop_loss"));
    EXPECT_FALSE(body.contains("take_profit"));
    EXPECT_EQ(body["type"], "market");
    EXPECT_EQ(body["side"], "buy");
    EXPECT_EQ(body["time_in_force"], "day");

    // Adding to a position (or opening one) is still an entry: bracket
    Order add(OrderType::SELL, "MDT", 1, 105.48f);
    add.setStopLoss(5);
    add.setTakeProfit(1);
    add.setPositionBefore(-2);
    EXPECT_FALSE(add.reducesPosition());
    EXPECT_EQ(AlpacaBroker::buildOrderJson(add)["order_class"], "bracket");
}

TEST(AlpacaBrokerTests, RemainingSharesGetAStopAndTargetAfterAPartialExit)
{
    // Short 3, buy 1 back at 100 (its own levels: stop 95, target 110, as if it were a long entry).
    // The 2 still short need a stop ABOVE (105) and a target BELOW (90): same distances, mirrored.
    Order exit(OrderType::BUY, "MDT", 1, 100.0f);
    exit.setStopLoss(5);
    exit.setTakeProfit(10);
    exit.setPositionBefore(-3);

    json protect = AlpacaBroker::buildProtectionJson(exit);
    ASSERT_FALSE(protect.is_null());
    EXPECT_EQ(protect["order_class"], "oco");
    EXPECT_EQ(protect["side"], "buy");          // closes the rest of the short
    EXPECT_EQ(protect["qty"], "2");
    EXPECT_EQ(protect["type"], "limit");
    EXPECT_EQ(protect["time_in_force"], "gtc");
    EXPECT_EQ(protect["stop_loss"]["stop_price"], "105.00");
    EXPECT_EQ(protect["take_profit"]["limit_price"], "90.00");

    // Long 2, sell 1 at 200 (its levels: stop 210, target 190) -> the 1 left: stop 190, target 210
    Order sell(OrderType::SELL, "TXN", 1, 200.0f);
    sell.setStopLoss(5);
    sell.setTakeProfit(5);
    sell.setPositionBefore(2);
    json longProtect = AlpacaBroker::buildProtectionJson(sell);
    EXPECT_EQ(longProtect["side"], "sell");
    EXPECT_EQ(longProtect["stop_loss"]["stop_price"], "190.00");
    EXPECT_EQ(longProtect["take_profit"]["limit_price"], "210.00");

    // Closing the whole position (or an entry) leaves nothing to protect
    exit.setPositionBefore(-1);
    EXPECT_TRUE(AlpacaBroker::buildProtectionJson(exit).is_null());
    Order entry(OrderType::BUY, "TXN", 1, 100.0f);
    entry.setStopLoss(5);
    entry.setTakeProfit(5);
    EXPECT_TRUE(AlpacaBroker::buildProtectionJson(entry).is_null());
}

TEST(AlpacaBrokerTests, WorkingOrderStatuses)
{
    for (const char* s : {"new", "accepted", "pending_new", "partially_filled"})
        EXPECT_TRUE(AlpacaBroker::isWorkingStatus(s)) << s;
    for (const char* s : {"filled", "canceled", "rejected", "expired"})
        EXPECT_FALSE(AlpacaBroker::isWorkingStatus(s)) << s;
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

    MarketCondition condition = AlpacaApi::parseMinuteBar(bar, "NVDA");

    EXPECT_EQ(condition.DateTime, "2026-10-28 10:31:00"); // 14:31 UTC = 10:31 New York (EDT)
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

    MarketCondition condition = AlpacaApi::parseMinuteBar(bar, "NVDA");

    EXPECT_EQ(condition.Ticker, "NVDA");
    EXPECT_EQ(condition.DateTime, "2026-10-27 15:59:00"); // 19:59 UTC = 15:59 New York (EDT)
}

TEST(AlpacaStreamTests, TestFeedUsesFakeSymbol)
{
    json config = {{"ticker", "NVDA"}, {"alpaca", {{"data_feed", "test"}}}};
    AlpacaStream stream(config);
    EXPECT_EQ(stream.getSymbol(), "FAKEPACA");
}

TEST(AlpacaStreamTests, SubscribesToEveryPortfolioTicker)
{
    json config = {{"ticker", "NVDA"}, {"alpaca", {{"data_feed", "iex"}}}};
    AlpacaStream stream(config);
    EXPECT_EQ(stream.getSymbols(), std::vector<std::string>{"NVDA"});

    stream.setSymbols({"TXN", "DHR", "WMT"});
    EXPECT_EQ(stream.getSymbols().size(), 3u);
    EXPECT_EQ(stream.getSymbol(), "TXN");

    // The test feed only has its fake symbol
    json test = {{"ticker", "NVDA"}, {"alpaca", {{"data_feed", "test"}}}};
    AlpacaStream fake(test);
    fake.setSymbols({"TXN", "DHR"});
    EXPECT_EQ(fake.getSymbols(), std::vector<std::string>{"FAKEPACA"});
}

TEST(AlpacaStreamTests, BarsKeepTheirOwnTicker)
{
    json bar = json::parse(R"({"T":"b","S":"WMT","o":10,"c":11,"v":5,"t":"2026-10-27T19:59:00Z"})");
    EXPECT_EQ(AlpacaApi::parseMinuteBar(bar, "TXN").Ticker, "WMT");
}

// ---------- Rolling window of live bars ----------

TEST(MarketDataLiveTests, AddBarKeepsOnlyNewestBars)
{
    MarketData marketData;
    for (int i = 0; i < 10; i++)
        marketData.addBar(MarketCondition("t" + std::to_string(i), "NVDA", i, i, 1, "1m"), 3);

    // Trimmed in batches: never fewer than 3 bars, never more than 6, newest always kept
    for (int i = 10; i < 100; i++) {
        marketData.addBar(MarketCondition("t" + std::to_string(i), "NVDA", i, i, 1, "1m"), 3);
        const auto& data = marketData.getData();
        ASSERT_GE(data.size(), 3u);
        ASSERT_LE(data.size(), 6u);
        ASSERT_EQ(data.back().DateTime, "t" + std::to_string(i));
        ASSERT_EQ(data[data.size() - 3].DateTime, "t" + std::to_string(i - 2));   // last 3 in order
    }
}

TEST(AlpacaBrokerTests, ClientOrderIdIsSentWhenSet)
{
    Order order(OrderType::BUY, "NVDA", 1, 100.0f);
    EXPECT_FALSE(AlpacaBroker::buildOrderJson(order).contains("client_order_id"));

    order.setClientOrderId("algo-12-1793300000");
    EXPECT_EQ(AlpacaBroker::buildOrderJson(order)["client_order_id"], "algo-12-1793300000");
}

TEST(AlpacaDailyBarsTests, ParsesDailyBarsAsNewYorkDates)
{
    // Alpaca stamps daily bars at midnight New York time (04:00Z in summer, 05:00Z in winter)
    json body = json::parse(R"({"bars":[
        {"t":"2026-10-29T04:00:00Z","o":70.1,"h":70.9,"l":69.5,"c":70.5,"v":1234567},
        {"t":"2026-12-01T05:00:00Z","o":71.0,"h":72.0,"l":70.0,"c":71.5,"v":2000000},
        {"t":"2026-12-02T05:00:00Z","o":71.5,"h":72.5,"l":71.0,"c":72.0,"v":10}
    ],"next_page_token":null})");

    std::vector<quant::DailyBar> bars = AlpacaDailyBars::parseBars(body, "2026-12-02");   // today: unfinished
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].date, "2026-10-29");
    EXPECT_EQ(bars[1].date, "2026-12-01");
    EXPECT_DOUBLE_EQ(bars[0].close, 70.5);
    EXPECT_DOUBLE_EQ(bars[0].high, 70.9);
    EXPECT_DOUBLE_EQ(bars[1].volume, 2000000);

    EXPECT_TRUE(AlpacaDailyBars::parseBars(json::object()).empty());
}
