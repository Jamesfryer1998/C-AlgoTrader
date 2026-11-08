#include <gtest/gtest.h>
#include <filesystem>
#include "FakeAlpacaClient.hpp"
#include "../../src/alpaca/AlpacaApi.hpp"
#include "../../src/alpaca/AlpacaDailyBars.hpp"
#include "../../src/alpaca/AlpacaStream.hpp"

namespace fs = std::filesystem;

/**
 * AlpacaStream's messages and back-fill, AlpacaDailyBars' downloads, and AlpacaApi's helpers, against a fake
 * Alpaca (no network).
 */

// ---------- AlpacaStream ----------

namespace
{
    json iexConfig() { return {{"alpaca", {{"data_feed", "iex"}}}}; }
}

TEST(AlpacaStreamMessageTests, LogsInThenSubscribesToEveryStock)
{
    auto alpaca = std::make_shared<FakeAlpacaClient>();
    AlpacaStream stream(iexConfig(), {"KO", "PEP"}, alpaca);

    std::optional<json> login = stream.handleAlpacaMessage({{"T", "success"}, {"msg", "connected"}}, nullptr);
    ASSERT_TRUE(login);
    EXPECT_EQ((*login)["action"], "auth");
    EXPECT_EQ((*login)["key"], "PKTEST");
    EXPECT_EQ((*login)["secret"], "SECRET");

    std::optional<json> subscribe = stream.handleAlpacaMessage({{"T", "success"}, {"msg", "authenticated"}}, nullptr);
    ASSERT_TRUE(subscribe);
    EXPECT_EQ((*subscribe)["action"], "subscribe");
    EXPECT_EQ((*subscribe)["bars"], json({"KO", "PEP"}));

    // Nothing to send back for the subscription's confirmation or an error
    EXPECT_FALSE(stream.handleAlpacaMessage({{"T", "subscription"}, {"bars", {"KO", "PEP"}}}, nullptr));
    EXPECT_FALSE(stream.handleAlpacaMessage({{"T", "error"}, {"code", 406}, {"msg", "connection limit exceeded"}}, nullptr));
}

TEST(AlpacaStreamMessageTests, ABarGoesToTheCallbackWithItsStock)
{
    AlpacaStream stream(iexConfig(), {"KO", "PEP"}, std::make_shared<FakeAlpacaClient>());
    std::vector<MarketCondition> received;
    auto onBar = [&](const MarketCondition& bar) { received.push_back(bar); };

    EXPECT_FALSE(stream.handleAlpacaMessage({{"T", "b"}, {"S", "PEP"}, {"t", "2026-10-26T14:00:00Z"},
                                             {"o", 150.0}, {"c", 150.5}, {"v", 1200}}, onBar));
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].Ticker, "PEP");
    EXPECT_EQ(received[0].DateTime, "2026-10-26 10:00:00");
    EXPECT_FLOAT_EQ(received[0].Close, 150.5f);
}

TEST(AlpacaStreamMessageTests, BackfillAsksForTheNewestBarsAndReturnsThemOldestFirst)
{
    auto alpaca = std::make_shared<FakeAlpacaClient>();
    alpaca->reply("GET", "/v2/stocks/KO/bars", 200, {{"bars", {
        {{"t", "2026-10-26T14:01:00Z"}, {"o", 60.1}, {"c", 60.1}, {"v", 10}},
        {{"t", "2026-10-26T14:00:00Z"}, {"o", 60.0}, {"c", 60.0}, {"v", 10}}}}});
    AlpacaStream stream(iexConfig(), {"KO"}, alpaca);

    std::vector<MarketCondition> bars = stream.history("KO", 2);
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].DateTime, "2026-10-26 10:00:00");
    EXPECT_EQ(bars[1].DateTime, "2026-10-26 10:01:00");
    const std::string& url = alpaca->requests[0].url;
    EXPECT_NE(url.find("limit=2"), std::string::npos);
    EXPECT_NE(url.find("sort=desc"), std::string::npos);
    EXPECT_NE(url.find("feed=iex"), std::string::npos);

    // Alpaca unreachable: no history, the strategies wait for live bars
    EXPECT_TRUE(stream.history("PEP", 2).empty());
}

// ---------- AlpacaDailyBars ----------

namespace
{
    struct AlpacaDailyBarsDownloadTests : public ::testing::Test
    {
        std::shared_ptr<FakeAlpacaClient> alpaca = std::make_shared<FakeAlpacaClient>();
        fs::path folder;

        void SetUp() override
        {
            folder = fs::temp_directory_path()
                   / ("daily_bars_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
            fs::remove_all(folder);
        }
        void TearDown() override { fs::remove_all(folder); }

        static json day(const std::string& date, double close)
        {
            return {{"t", date + "T04:00:00Z"}, {"o", close}, {"h", close}, {"l", close}, {"c", close}, {"v", 1e6}};
        }
    };
}

TEST_F(AlpacaDailyBarsDownloadTests, DownloadsEveryPageAndSavesThem)
{
    alpaca->reply("GET", "/v2/stocks/KO/bars", 200, {{"bars", {day("2026-10-26", 60.0)}}, {"next_page_token", "more"}});
    alpaca->reply("GET", "/v2/stocks/KO/bars", 200, {{"bars", {day("2026-10-27", 61.0)}}});
    AlpacaDailyBars daily(json::object(), folder.string(), alpaca);

    std::vector<quant::DailyBar> bars = daily.load("KO", "2026-10-26", "2026-10-27", false);
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].date, "2026-10-26");
    EXPECT_DOUBLE_EQ(bars[1].close, 61.0);
    EXPECT_TRUE(fs::exists(daily.csvPath("KO")));
    EXPECT_NE(alpaca->requests[1].url.find("page_token=more"), std::string::npos);
    EXPECT_NE(alpaca->requests[0].url.find("timeframe=1Day"), std::string::npos);
}

TEST_F(AlpacaDailyBarsDownloadTests, FallsBackToTheSavedFile)
{
    alpaca->reply("GET", "/v2/stocks/KO/bars", 200, {{"bars", {day("2026-10-26", 60.0)}}});
    AlpacaDailyBars daily(json::object(), folder.string(), alpaca);
    ASSERT_EQ(daily.load("KO", "2026-10-26", "2026-10-26", false).size(), 1u);

    // Offline: the saved file, no request
    size_t asked = alpaca->requests.size();
    EXPECT_EQ(daily.load("KO", "2026-10-26", "2026-10-26", true).size(), 1u);
    EXPECT_EQ(alpaca->requests.size(), asked);

    // A failed download: the saved file too
    auto failing = std::make_shared<FakeAlpacaClient>();
    failing->reply("GET", "/v2/stocks/KO/bars", 403, {{"message", "forbidden"}});
    AlpacaDailyBars fallback(json::object(), folder.string(), failing);
    EXPECT_EQ(fallback.load("KO", "2026-10-26", "2026-10-26", false).size(), 1u);
}

TEST_F(AlpacaDailyBarsDownloadTests, LoadAllStopsAtTheFirstStockWithNoBars)
{
    alpaca->reply("GET", "/v2/stocks/KO/bars", 200, {{"bars", {day("2026-10-26", 60.0)}}});
    AlpacaDailyBars daily(json::object(), folder.string(), alpaca);
    std::map<std::string, std::vector<quant::DailyBar>> out;
    EXPECT_TRUE(daily.loadAll({"KO"}, "2026-10-26", "2026-10-26", false, out));
    EXPECT_FALSE(daily.loadAll({"KO", "PEP"}, "2026-10-26", "2026-10-26", false, out));   // PEP: no reply, no file
}

TEST_F(AlpacaDailyBarsDownloadTests, NoKeysMeansNoDownload)
{
    auto noKeys = std::make_shared<FakeAlpacaClient>(false);
    AlpacaDailyBars daily(json::object(), folder.string(), noKeys);
    EXPECT_TRUE(daily.load("KO", "2026-10-26", "2026-10-26", false).empty());
    EXPECT_TRUE(noKeys->requests.empty());
}

// ---------- AlpacaApi ----------

TEST(AlpacaApiTests, BarsUrl)
{
    EXPECT_EQ(AlpacaApi::barsUrl("https://data.alpaca.markets", "KO", "1Min"),
              "https://data.alpaca.markets/v2/stocks/KO/bars?timeframe=1Min");
}

TEST(AlpacaApiTests, NumbersArriveAsText)
{
    json body = {{"cash", "1234.56"}, {"bad", "n/a"}, {"number", 5}};
    EXPECT_DOUBLE_EQ(AlpacaApi::numberField(body, "cash"), 1234.56);
    EXPECT_DOUBLE_EQ(AlpacaApi::numberField(body, "bad"), 0);       // not a number
    EXPECT_DOUBLE_EQ(AlpacaApi::numberField(body, "missing"), 0);
    EXPECT_DOUBLE_EQ(AlpacaApi::numberField(body, "number"), 0);    // Alpaca sends these as text only
}

TEST(AlpacaApiTests, NextPageToken)
{
    EXPECT_EQ(AlpacaApi::nextPageToken({{"next_page_token", "abc"}}), "abc");
    EXPECT_EQ(AlpacaApi::nextPageToken({{"next_page_token", nullptr}}), "");   // the last page
    EXPECT_EQ(AlpacaApi::nextPageToken(json::array()), "");
}

TEST(AlpacaApiTests, MinuteBarWithABadTime)
{
    MarketCondition bar = AlpacaApi::parseMinuteBar({{"t", "yesterday"}, {"c", 1.0}}, "KO");
    EXPECT_TRUE(bar.DateTime.empty());   // AlpacaHistory drops these
    EXPECT_EQ(bar.Ticker, "KO");
}
