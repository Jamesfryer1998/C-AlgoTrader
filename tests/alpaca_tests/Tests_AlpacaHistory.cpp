#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "FakeAlpacaClient.hpp"
#include "../../src/alpaca/AlpacaHistory.hpp"
#include "../../src/data_access/CSVParser.hpp"
#include "../../src/util/MarketTime.hpp"

namespace fs = std::filesystem;

/**
 * AlpacaHistory against a fake Alpaca: paging, retries, and the CSV files it writes. October 2026 is
 * daylight saving time in New York (UTC-4), so 13:30 UTC is the 09:30 open.
 */
namespace
{
    const std::string BARS = "/v2/stocks/KO/bars";

    json minuteBar(const std::string& utc, double close)
    {
        return {{"t", utc}, {"o", close}, {"c", close}, {"v", 1000}};
    }

    // 26 Oct 2026: closes 16:00 New York, 20:00 UTC
    const std::string DAY = "2026-10-26";
    const std::time_t MIDDAY = MarketTime::parseUtc("2026-10-26T16:00:00Z");      // 12:00 in New York
    const std::time_t EVENING = MarketTime::parseUtc("2026-10-26T23:00:00Z");     // 19:00, long after the close

    // Pretend the file was written at `utc`
    void setWrittenAt(const fs::path& path, std::time_t utc)
    {
        fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::seconds(std::time(nullptr) - utc));
    }

    struct AlpacaHistoryTests : public ::testing::Test
    {
        std::shared_ptr<FakeAlpacaClient> alpaca = std::make_shared<FakeAlpacaClient>();
        fs::path folder;

        void SetUp() override
        {
            folder = fs::temp_directory_path()
                   / ("alpaca_history_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
            fs::remove_all(folder);
        }
        void TearDown() override { fs::remove_all(folder); }

        AlpacaHistory history()
        {
            AlpacaHistory history({{"ticker", "KO"}, {"alpaca", {{"history_feed", "sip"}}}}, alpaca);
            history.setQuiet(true);
            return history;
        }

        std::vector<MarketCondition> readDay(const std::string& day)
        {
            CSVParser parser;
            parser.Read((folder / ("marketData_KO_" + day + ".csv")).string());
            return parser.GetData();
        }
    };
}

TEST_F(AlpacaHistoryTests, FollowsThePagesAndConvertsToNewYorkTime)
{
    alpaca->reply("GET", BARS, 200, {{"bars", {minuteBar("2026-10-26T13:30:00Z", 60.0)}}, {"next_page_token", "page2"}});
    alpaca->reply("GET", BARS, 200, {{"bars", {minuteBar("2026-10-26T13:31:00Z", 60.1)}}, {"next_page_token", nullptr}});

    bool ok = false;
    std::vector<MarketCondition> bars = history().fetchBars("2026-10-26T00:00:00Z", "2026-10-27T00:00:00Z", ok);

    EXPECT_TRUE(ok);
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].DateTime, "2026-10-26 09:30:00");
    EXPECT_EQ(bars[1].DateTime, "2026-10-26 09:31:00");
    EXPECT_EQ(bars[0].Ticker, "KO");

    std::vector<FakeAlpacaClient::Request> asked = alpaca->sent("GET", BARS);
    ASSERT_EQ(asked.size(), 2u);
    EXPECT_EQ(asked[0].url.find("page_token"), std::string::npos);
    EXPECT_NE(asked[1].url.find("&page_token=page2"), std::string::npos);
    EXPECT_NE(asked[0].url.find("feed=sip"), std::string::npos);
    EXPECT_NE(asked[0].url.find("adjustment=split"), std::string::npos);
}

TEST_F(AlpacaHistoryTests, WaitsLongerEachTimeAlpacaIsBusy)
{
    alpaca->reply("GET", BARS, 429, {{"message", "too many requests"}});
    alpaca->reply("GET", BARS, 429, {{"message", "too many requests"}});
    alpaca->reply("GET", BARS, 200, {{"bars", {minuteBar("2026-10-26T13:30:00Z", 60.0)}}});

    bool ok = false;
    std::vector<MarketCondition> bars = history().fetchBars("2026-10-26T00:00:00Z", "2026-10-27T00:00:00Z", ok);
    EXPECT_TRUE(ok);
    EXPECT_EQ(bars.size(), 1u);
    ASSERT_EQ(alpaca->waits.size(), 2u);
    EXPECT_EQ(alpaca->waits[1], 2 * alpaca->waits[0]);
}

TEST_F(AlpacaHistoryTests, GivesUpWhenAlpacaKeepsFailing)
{
    alpaca->reply("GET", BARS, 403, {{"message", "subscription does not permit querying recent SIP data"}});
    bool ok = true;
    EXPECT_TRUE(history().fetchBars("2026-10-26T00:00:00Z", "2026-10-27T00:00:00Z", ok).empty());
    EXPECT_FALSE(ok);
    EXPECT_EQ(alpaca->sent("GET").size(), 1u);   // 403 isn't "busy": no point asking again
}

TEST_F(AlpacaHistoryTests, WritesOneFilePerTradingDayInRegularHours)
{
    alpaca->reply("GET", BARS, 200, {{"bars", {
        minuteBar("2026-10-26T12:00:00Z", 59.0),    // 08:00: before the open, dropped
        minuteBar("2026-10-26T13:30:00Z", 60.0),
        minuteBar("2026-10-26T19:59:00Z", 60.5),    // 15:59: the last bar
        minuteBar("2026-10-27T13:30:00Z", 61.0)}}});

    EXPECT_EQ(history().downloadToCsv("2026-10-26", "2026-10-27", folder.string()), 3);

    std::vector<MarketCondition> first = readDay("2026-10-26");
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0].DateTime, "2026-10-26 09:30:00");
    EXPECT_FLOAT_EQ(first[1].Close, 60.5f);
    EXPECT_EQ(readDay("2026-10-27").size(), 1u);
}

TEST_F(AlpacaHistoryTests, SkipsDaysAlreadySavedUnlessOverwriting)
{
    alpaca->reply("GET", BARS, 200, {{"bars", {minuteBar("2026-10-26T13:30:00Z", 60.0)}}});
    EXPECT_EQ(history().downloadToCsv("2026-10-26", "2026-10-26", folder.string()), 1);
    EXPECT_EQ(history().downloadToCsv("2026-10-26", "2026-10-26", folder.string()), 0);   // already there
    EXPECT_EQ(history().downloadToCsv("2026-10-26", "2026-10-26", folder.string(), true), 1);
}

TEST_F(AlpacaHistoryTests, ADayIsCompleteAtItsLastBarOrWhenSavedAfterTheClose)
{
    fs::create_directories(folder);
    fs::path whole = folder / "whole.csv", morning = folder / "morning.csv", other = folder / "other.csv";
    std::ofstream(whole) << MARKET_DATA_CSV_HEADER << "\n" << DAY << " 09:30:00,KO,60,60,1000,1m\n"
                         << DAY << " 15:59:00,KO,60,60,1000,1m\n";
    std::ofstream(morning) << MARKET_DATA_CSV_HEADER << "\n" << DAY << " 09:30:00,KO,60,60,1000,1m\n";
    std::ofstream(other) << MARKET_DATA_CSV_HEADER << "\n2026-10-19 09:30:00,KO,60,60,1000,1m\n";

    setWrittenAt(whole, MIDDAY);
    EXPECT_TRUE(AlpacaHistory::isCompleteDay(whole.string(), DAY));      // it reaches 15:59

    setWrittenAt(morning, MIDDAY);
    EXPECT_FALSE(AlpacaHistory::isCompleteDay(morning.string(), DAY));   // saved while the day was trading
    setWrittenAt(morning, EVENING);
    EXPECT_TRUE(AlpacaHistory::isCompleteDay(morning.string(), DAY));    // all Alpaca had (an early close, a halt)

    EXPECT_FALSE(AlpacaHistory::isCompleteDay(other.string(), DAY));     // a different day's bars
    EXPECT_FALSE(AlpacaHistory::isCompleteDay((folder / "missing.csv").string(), DAY));
}

TEST_F(AlpacaHistoryTests, ADaySavedWhileItWasTradingIsDownloadedAgain)
{
    alpaca->reply("GET", BARS, 200, {{"bars", {minuteBar("2026-10-26T13:30:00Z", 60.0)}}});
    EXPECT_EQ(history().downloadToCsv(DAY, DAY, folder.string()), 1);
    setWrittenAt(folder / ("marketData_KO_" + DAY + ".csv"), MIDDAY);   // as if downloaded at noon that day

    alpaca->reply("GET", BARS, 200, {{"bars", {minuteBar("2026-10-26T13:30:00Z", 60.0),
                                               minuteBar("2026-10-26T19:59:00Z", 61.0)}}});
    EXPECT_EQ(history().downloadToCsv(DAY, DAY, folder.string()), 2);   // the rest of the day
    EXPECT_EQ(readDay(DAY).size(), 2u);
}

TEST_F(AlpacaHistoryTests, RemembersFinishedMonths)
{
    alpaca->reply("GET", BARS, 200, {{"bars", {minuteBar("2026-09-01T13:30:00Z", 60.0)}}});
    EXPECT_EQ(history().downloadToCsv("2026-09-01", "2026-09-30", folder.string()), 1);
    size_t asked = alpaca->sent("GET").size();

    EXPECT_EQ(history().downloadToCsv("2026-09-01", "2026-09-30", folder.string()), 0);
    EXPECT_EQ(alpaca->sent("GET").size(), asked);   // September is done: not asked again
    EXPECT_TRUE(fs::exists(folder / ".alpaca_history_KO.json"));
}

TEST_F(AlpacaHistoryTests, BadDatesOrAFailedDownload)
{
    EXPECT_EQ(history().downloadToCsv("2026-10-27", "2026-10-26", folder.string()), -1);   // ends before it starts
    EXPECT_EQ(history().downloadToCsv("26/10/2026", "2026-10-27", folder.string()), -1);
    EXPECT_TRUE(alpaca->requests.empty());

    alpaca->reply("GET", BARS, 500, {{"message", "internal error"}});
    EXPECT_EQ(history().downloadToCsv("2026-10-26", "2026-10-26", folder.string()), -1);
}
