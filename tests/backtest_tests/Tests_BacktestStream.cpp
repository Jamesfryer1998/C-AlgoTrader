#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>
#include "../../src/backtest/BacktestStream.hpp"

namespace fs = std::filesystem;

class BacktestStreamTests : public ::testing::Test
{
    protected:
        std::string root;

        void SetUp() override
        {
            root = (fs::temp_directory_path() / ("backtest_stream_test_" + std::to_string(::getpid()))).string();
            fs::remove_all(root);
            // AAA in data/minute/AAA/ (screen_app's layout), BBB in data/ itself (backtest_app's layout)
            for (const char* day : {"2026-03-02", "2026-03-03", "2026-03-04"})
            {
                write(root + "/minute/AAA", "AAA", day, 100);
                write(root, "BBB", day, 50);
            }
            write(root + "/minute/AAA", "AAA", "2026-03-05", 100);   // only AAA has this day
        }
        void TearDown() override { fs::remove_all(root); }

        static void write(const std::string& folder, const std::string& ticker, const std::string& day, float price)
        {
            fs::create_directories(folder);
            std::ofstream file(folder + "/marketData_" + ticker + "_" + day + ".csv");
            file << "Datetime,Ticker,Open,Close,Volume,TimeInterval\n";
            for (int m = 0; m < 3; m++)   // 09:30, 09:31, 09:32
                file << day << " 09:3" << m << ":00," << ticker << "," << price + m << "," << price + m << ",100,1m\n";
        }
};

TEST_F(BacktestStreamTests, FindsEachTickersSavedDays)
{
    EXPECT_EQ(BacktestStream::folderFor(root, "AAA"), root + "/minute/AAA");
    EXPECT_EQ(BacktestStream::folderFor(root, "BBB"), root);
    EXPECT_EQ(BacktestStream::savedDays(root, "AAA").size(), 4u);
    EXPECT_EQ(BacktestStream::savedDays(root, "BBB").back(), "2026-03-04");
}

TEST_F(BacktestStreamTests, DefaultsToTheLatestDayEveryStockHas)
{
    BacktestStream stream(root, {"AAA", "BBB"}, "", 1, 0);
    ASSERT_TRUE(stream.load());
    ASSERT_EQ(stream.getDays().size(), 1u);
    EXPECT_EQ(stream.getDays()[0], "2026-03-04");   // not 03-05: BBB has nothing then
    EXPECT_EQ(stream.minutes(), 3u);

    // History is the days before, for back-fill
    std::vector<MarketCondition> history = stream.history("AAA", 4);
    ASSERT_EQ(history.size(), 4u);
    EXPECT_EQ(history.back().DateTime, "2026-03-03 09:32:00");
    EXPECT_EQ(stream.history("BBB", 100).size(), 6u);   // all there is
}

TEST_F(BacktestStreamTests, PlaysEveryStocksBarMinuteByMinuteInPortfolioOrder)
{
    BacktestStream stream(root, {"BBB", "AAA"}, "2026-03-02", 2, 0);
    ASSERT_TRUE(stream.load());
    EXPECT_EQ(stream.getDays(), (std::vector<std::string>{"2026-03-02", "2026-03-03"}));

    std::vector<std::string> seen;
    std::atomic<bool> finished{false};
    stream.start([&](const MarketCondition& bar) { seen.push_back(bar.DateTime.substr(11, 5) + " " + bar.Ticker); },
                 [&] { finished = true; });
    for (int i = 0; i < 200 && !finished; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    stream.stop();

    ASSERT_TRUE(finished);
    ASSERT_EQ(seen.size(), 12u);   // 2 days x 3 minutes x 2 stocks
    EXPECT_EQ(seen[0], "09:30 BBB");
    EXPECT_EQ(seen[1], "09:30 AAA");
    EXPECT_EQ(seen[2], "09:31 BBB");
}

TEST_F(BacktestStreamTests, MissingDataIsReported)
{
    BacktestStream unknown(root, {"AAA", "ZZZ"}, "", 1, 0);
    EXPECT_FALSE(unknown.load());

    BacktestStream tooLate(root, {"AAA", "BBB"}, "2026-04-01", 1, 0);
    EXPECT_FALSE(tooLate.load());

    BacktestStream between(root, {"AAA"}, "2026-03-01", 1, 0);   // a Sunday: starts at the next saved day
    ASSERT_TRUE(between.load());
    EXPECT_EQ(between.getDays()[0], "2026-03-02");
}
