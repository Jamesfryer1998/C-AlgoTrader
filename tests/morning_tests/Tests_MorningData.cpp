#include <gtest/gtest.h>
#include <filesystem>
#include "FakeMarket.hpp"
#include "alpaca_tests/FakeAlpacaClient.hpp"
#include "alpaca/AlpacaMultiBars.hpp"
#include "morning/MorningData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string DAY = "2026-03-02";   // New York is on winter time: 09:30 is 14:30 UTC

    class MorningDataTests : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                folder = (fs::temp_directory_path() / ("morning_data_" + std::to_string(::getpid()))).string();
                fs::remove_all(folder);
            }
            void TearDown() override { fs::remove_all(folder); }

            std::string folder;
    };
}

TEST(AlpacaMultiBarsTests, AsksForManyStocksAtOnceAndFollowsPages)
{
    auto alpaca = std::make_shared<FakeAlpacaClient>();
    json firstPage = {{"bars", {{"AAPL", {{{"t", "2026-03-02T14:30:00Z"}, {"o", 1}, {"h", 2}, {"l", 0.5}, {"c", 1.5}, {"v", 100}}}}}},
                      {"next_page_token", "p2"}};
    json secondPage = {{"bars", {{"AAPL", {{{"t", "2026-03-02T14:31:00Z"}, {"o", 1.5}, {"h", 2}, {"l", 1}, {"c", 1.8}, {"v", 50}}}},
                                 {"MSFT", {{{"t", "2026-03-02T14:30:00Z"}, {"o", 3}, {"h", 4}, {"l", 2}, {"c", 3.5}, {"v", 70}}}}}},
                       {"next_page_token", nullptr}};
    alpaca->reply("GET", "/v2/stocks/bars?symbols=AAPL,MSFT", 200, firstPage);
    alpaca->reply("GET", "/v2/stocks/bars?symbols=AAPL,MSFT", 200, secondPage);   // its second page
    alpaca->reply("GET", "/v2/stocks/bars?symbols=NVDA", 200, {{"bars", json::object()}});

    AlpacaMultiBars multi(json::object(), alpaca);
    bool ok = false;
    auto bars = multi.fetch({"AAPL", "MSFT", "NVDA"}, "1Min", "2026-03-02T14:30:00Z", "2026-03-02T21:00:00Z", 2, ok);
    EXPECT_TRUE(ok);
    EXPECT_EQ(alpaca->sent("GET").size(), 3u);   // two pages of the first batch, then the second batch
    ASSERT_EQ(bars["AAPL"].size(), 2u);
    EXPECT_EQ(bars["AAPL"][0].time, "2026-03-02 09:30:00");
    EXPECT_DOUBLE_EQ(bars["AAPL"][0].high, 2);
    EXPECT_DOUBLE_EQ(bars["AAPL"][0].low, 0.5);
    EXPECT_EQ(bars["MSFT"].size(), 1u);
    EXPECT_FALSE(bars.count("NVDA"));
    EXPECT_NE(alpaca->sent("GET")[0].url.find("feed=sip"), std::string::npos);
    EXPECT_NE(alpaca->sent("GET")[0].url.find("adjustment=split"), std::string::npos);
}

TEST(AlpacaMultiBarsTests, KeepsOnlyPlainListedSymbols)
{
    auto listed = [](const std::string& symbol, const std::string& exchange, bool tradable = true) {
        return json{{"symbol", symbol}, {"exchange", exchange}, {"tradable", tradable}, {"status", "active"}};
    };
    EXPECT_TRUE(AlpacaMultiBars::isListedStock(listed("AAPL", "NASDAQ")));
    EXPECT_TRUE(AlpacaMultiBars::isListedStock(listed("SPY", "ARCA")));
    EXPECT_FALSE(AlpacaMultiBars::isListedStock(listed("BRK.B", "NYSE")));
    EXPECT_FALSE(AlpacaMultiBars::isListedStock(listed("GBTC", "OTC")));
    EXPECT_FALSE(AlpacaMultiBars::isListedStock(listed("XYZ", "NYSE", false)));
    EXPECT_FALSE(AlpacaMultiBars::isListedStock(listed("ABCDEF", "NYSE")));
}

TEST_F(MorningDataTests, DownloadsOnceThenReadsTheSavedFiles)
{
    auto market = std::make_shared<FakeMarket>(std::vector<std::string>{"AAA", "BBB"});
    {
        MorningData data(json::object(), folder, false, market);
        std::vector<std::string> symbols;
        ASSERT_TRUE(data.symbols(symbols));
        EXPECT_EQ(symbols, (std::vector<std::string>{"AAA", "BBB"}));
        ASSERT_TRUE(data.ensureDailyBars(symbols, "2026-02-23", "2026-03-06", nullptr));
        EXPECT_EQ(data.dailyBars("AAA").size(), 10u);   // two weeks of weekdays
        auto openings = data.openingBars(DAY, symbols, 5);
        ASSERT_EQ(openings.size(), 2u);
        EXPECT_EQ(openings["AAA"].time, DAY + " 09:30:00");
        EXPECT_NE(market->requests.back().find("timeframe=5Min"), std::string::npos);
        auto minutes = data.minuteBars(DAY, {"AAA"});
        ASSERT_EQ(minutes["AAA"].size(), 390u);
        EXPECT_EQ(minutes["AAA"].back().time, DAY + " 15:59:00");
    }
    size_t asked = market->requests.size();

    MorningData again(json::object(), folder, false, market);
    std::vector<std::string> symbols;
    ASSERT_TRUE(again.symbols(symbols));
    ASSERT_TRUE(again.ensureDailyBars(symbols, "2026-02-23", "2026-03-06", nullptr));
    EXPECT_EQ(again.openingBars(DAY, symbols, 5).size(), 2u);
    EXPECT_EQ(again.minuteBars(DAY, {"AAA"})["AAA"].size(), 390u);
    EXPECT_EQ(market->requests.size(), asked) << "nothing asked twice";

    again.minuteBars(DAY, {"AAA", "BBB"});
    EXPECT_EQ(market->requests.size(), asked + 1) << "only the new stock";
    EXPECT_NE(market->requests.back().find("symbols=BBB&"), std::string::npos);
}

TEST_F(MorningDataTests, OfflineNeverAsks)
{
    auto market = std::make_shared<FakeMarket>(std::vector<std::string>{"AAA"});
    MorningData data(json::object(), folder, true, market);
    std::vector<std::string> symbols;
    EXPECT_FALSE(data.symbols(symbols));
    EXPECT_TRUE(data.openingBars(DAY, {"AAA"}, 5).empty());
    EXPECT_TRUE(data.minuteBars(DAY, {"AAA"}).empty());
    EXPECT_TRUE(market->requests.empty());
}
