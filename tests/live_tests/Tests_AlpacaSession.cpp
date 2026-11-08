#include <gtest/gtest.h>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../alpaca_tests/FakeAlpacaClient.hpp"
#include "../../src/alpaca/AlpacaApi.hpp"
#include "../../src/live/BacktestDownloader.hpp"
#include "../../src/live/BrokerSession.hpp"
#include "../../src/util/MarketTime.hpp"

namespace fs = std::filesystem;

/**
 * The live app's Alpaca side against a fake Alpaca: the session (connecting, the start-up summary's lines,
 * the recap) and the backtest downloader (which days it fetches, and remembering days that have no bars).
 */

// ---------- AlpacaSession ----------

namespace
{
    struct AlpacaSessionTests : public ::testing::Test
    {
        std::shared_ptr<FakeAlpacaClient> alpaca = std::make_shared<FakeAlpacaClient>();
        OrderManagement oms;
        PerformanceTracker tracker{100000};

        static json config(bool dryRun = false)
        {
            return {{"alpaca", {{"trading_url", AlpacaApi::PAPER_TRADING_URL}, {"data_feed", "iex"}, {"dry_run", dryRun}}}};
        }

        void replyAccount()
        {
            alpaca->reply("GET", AlpacaApi::ACCOUNT_PATH, 200,
                          {{"status", "ACTIVE"}, {"cash", "98150.70"}, {"buying_power", "396385.69"},
                           {"equity", "100250.00"}, {"last_equity", "100000.00"}});
        }
    };
}

TEST_F(AlpacaSessionTests, ConnectsAndDescribesTheAccount)
{
    replyAccount();
    AlpacaSession session(config(), oms, tracker, alpaca);
    ASSERT_TRUE(session.connect());
    EXPECT_EQ(session.describeMode(), "LIVE on Alpaca's paper account");
    EXPECT_EQ(session.describeAccount(), "ACTIVE, cash $98,150.70, buying power $396,385.69");
    EXPECT_EQ(session.recordsFolder(), "live");
}

TEST_F(AlpacaSessionTests, ADryRunKeepsItsOwnRecords)
{
    replyAccount();
    AlpacaSession session(config(true), oms, tracker, alpaca);
    ASSERT_TRUE(session.connect());
    EXPECT_NE(session.describeMode().find("DRY RUN"), std::string::npos);
    EXPECT_EQ(session.recordsFolder(), "live_dry_run");
}

TEST_F(AlpacaSessionTests, NoAccountNoSession)
{
    alpaca->reply("GET", AlpacaApi::ACCOUNT_PATH, 401, {{"message", "unauthorized"}});
    AlpacaSession session(config(), oms, tracker, alpaca);
    EXPECT_FALSE(session.connect());
}

TEST_F(AlpacaSessionTests, MarketHoursFromAlpacasClock)
{
    std::time_t now = std::time(nullptr);
    alpaca->reply("GET", AlpacaApi::CLOCK_PATH, 200,
                  {{"is_open", false}, {"next_open", MarketTime::formatUtc(now + 3 * 3600 + 60)},
                   {"next_close", MarketTime::formatUtc(now + 10 * 3600)}});
    AlpacaSession session(config(), oms, tracker, alpaca);
    std::string hours = session.describeMarketHours();
    EXPECT_EQ(hours.rfind("closed, opens in 3h", 0), 0u) << hours;
    EXPECT_EQ(hours.find("holidays not checked"), std::string::npos);
}

TEST_F(AlpacaSessionTests, MarketHoursWithoutTheClockAreAnEstimate)
{
    AlpacaSession session(config(), oms, tracker, alpaca);   // no reply for the clock
    EXPECT_NE(session.describeMarketHours().find("regular hours, holidays not checked"), std::string::npos);
}

TEST_F(AlpacaSessionTests, TheRecapShowsEquityAndEachPosition)
{
    replyAccount();
    alpaca->reply("GET", AlpacaApi::POSITIONS_PATH + "/KO", 200,
                  {{"qty", "10"}, {"side", "long"}, {"avg_entry_price", "60.00"}, {"unrealized_pl", "5.00"},
                   {"unrealized_plpc", "0.0083"}});
    AlpacaSession session(config(), oms, tracker, alpaca);
    ASSERT_TRUE(session.connect());

    std::ostringstream recap;
    session.printSummary(recap, {"KO", "PEP"});
    EXPECT_NE(recap.str().find("Equity $100,250.00 (today +$250.00)"), std::string::npos) << recap.str();
    EXPECT_NE(recap.str().find("KO: 10 @ 60.00, unrealised +$5.00"), std::string::npos) << recap.str();
    EXPECT_NE(recap.str().find("PEP: no position"), std::string::npos);   // Alpaca's 404
}

// ---------- BacktestDownloader ----------

namespace
{
    // Weekdays in late October 2026: a backtest of the 26th needs the five before it too
    const std::vector<std::string> NEEDED = {"2026-10-19", "2026-10-20", "2026-10-21", "2026-10-22", "2026-10-23",
                                             "2026-10-26"};

    struct BacktestDownloaderTests : public ::testing::Test
    {
        std::shared_ptr<FakeAlpacaClient> alpaca = std::make_shared<FakeAlpacaClient>();
        fs::path root;
        std::string dataRoot;

        void SetUp() override
        {
            root = fs::temp_directory_path()
                 / ("backtest_downloader_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
            fs::remove_all(root);
            dataRoot = (root / "data").string() + "/";
        }
        void TearDown() override { fs::remove_all(root); }

        fs::path folder(const std::string& ticker) const { return fs::path(dataRoot) / "minute" / ticker; }

        void save(const std::string& ticker, const std::string& day)
        {
            fs::create_directories(folder(ticker));
            std::ofstream(folder(ticker) / ("marketData_" + ticker + "_" + day + ".csv"))
                << MARKET_DATA_CSV_HEADER << "\n" << day << " 09:30:00," << ticker << ",60,60,1000,1m\n";
        }

        // Alpaca's bars for `days`: one at each day's open (13:30 UTC in October)
        void replyBars(const std::string& ticker, const std::vector<std::string>& days)
        {
            json bars = json::array();
            for (const auto& day : days)
                bars.push_back({{"t", day + "T13:30:00Z"}, {"o", 60.0}, {"c", 60.0}, {"v", 1000}});
            alpaca->reply("GET", "/v2/stocks/" + ticker + "/bars", 200, {{"bars", bars}});
        }

        BacktestDownloader downloader() const { return BacktestDownloader(json::object(), dataRoot, alpaca); }
    };
}

TEST_F(BacktestDownloaderTests, NothingMissingNothingDownloaded)
{
    for (const auto& day : NEEDED)
        save("KO", day);
    EXPECT_TRUE(downloader().downloadMissing({"KO"}, "2026-10-26", 1));
    EXPECT_TRUE(alpaca->requests.empty());
}

TEST_F(BacktestDownloaderTests, DownloadsAStockMissingADay)
{
    for (const auto& day : NEEDED)
        save("KO", day);
    replyBars("PEP", NEEDED);

    EXPECT_TRUE(downloader().downloadMissing({"KO", "PEP"}, "2026-10-26", 1));
    EXPECT_TRUE(alpaca->sent("GET", "/stocks/KO/").empty());   // KO had everything
    EXPECT_FALSE(alpaca->sent("GET", "/stocks/PEP/").empty());
    for (const auto& day : NEEDED)
        EXPECT_TRUE(fs::exists(folder("PEP") / ("marketData_PEP_" + day + ".csv"))) << day;
}

TEST_F(BacktestDownloaderTests, RemembersDaysWithNoBarsSoTheyArentFetchedAgain)
{
    // Alpaca has nothing for the 21st (as on a holiday)
    std::vector<std::string> trading = NEEDED;
    trading.erase(trading.begin() + 2);
    replyBars("KO", trading);

    EXPECT_TRUE(downloader().downloadMissing({"KO"}, "2026-10-26", 1));
    std::ifstream remembered(folder("KO") / ".days_without_bars.json");
    json days = json::parse(remembered);
    EXPECT_EQ(days, json({"2026-10-21"}));

    size_t asked = alpaca->requests.size();
    EXPECT_TRUE(downloader().downloadMissing({"KO"}, "2026-10-26", 1));
    EXPECT_EQ(alpaca->requests.size(), asked);   // the 21st is known to be empty: no second download
}

TEST_F(BacktestDownloaderTests, AFailedDownloadStopsTheBacktest)
{
    alpaca->reply("GET", "/v2/stocks/KO/bars", 500, {{"message", "internal error"}});
    EXPECT_FALSE(downloader().downloadMissing({"KO"}, "2026-10-26", 1));
}
