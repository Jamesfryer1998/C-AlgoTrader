#include <gtest/gtest.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../src/live/LiveOptions.hpp"

namespace
{
    LiveOptions parse(std::vector<std::string> args)
    {
        args.insert(args.begin(), "algo_trader_app");
        std::vector<char*> argv;
        for (auto& arg : args)
            argv.push_back(arg.data());
        return LiveOptions::parse(static_cast<int>(argv.size()), argv.data());
    }
}

TEST(LiveOptionsTests, BacktestWithItsOwnCapital)
{
    LiveOptions options = parse({"--backtest", "2026-10-01", "--days", "31", "--capital", "25000", "--quiet"});
    EXPECT_TRUE(options.backtest);
    EXPECT_EQ(options.backtestDay, "2026-10-01");
    EXPECT_EQ(options.backtestDays, 31);
    EXPECT_DOUBLE_EQ(options.backtestCapital, 25000);
    EXPECT_TRUE(options.quiet);
}

TEST(LiveOptionsTests, CapitalDefaultsToTheConfig)
{
    EXPECT_DOUBLE_EQ(parse({"--backtest"}).backtestCapital, 0);
}

TEST(LiveOptionsTests, CapitalMustBeAboveZeroAndOnlyForBacktests)
{
    EXPECT_THROW(parse({"--backtest", "--capital", "0"}), std::invalid_argument);
    EXPECT_THROW(parse({"--backtest", "--capital", "-5"}), std::invalid_argument);
    EXPECT_THROW(parse({"--capital", "25000"}), std::invalid_argument);   // live uses Alpaca's own money
}

TEST(LiveOptionsTests, PositionsToStartABacktestWith)
{
    LiveOptions options = parse({"--backtest", "2026-11-09", "--positions", "KO:10@58.20,PEP:-5@170.1"});
    ASSERT_EQ(options.backtestPositions.size(), 2u);
    EXPECT_EQ(options.backtestPositions[0].ticker, "KO");
    EXPECT_DOUBLE_EQ(options.backtestPositions[0].quantity, 10);
    EXPECT_DOUBLE_EQ(options.backtestPositions[0].averagePrice, 58.20);
    EXPECT_DOUBLE_EQ(options.backtestPositions[1].quantity, -5);   // short
    EXPECT_TRUE(parse({"--backtest"}).backtestPositions.empty());

    EXPECT_THROW(parse({"--backtest", "--positions", "KO10"}), std::invalid_argument);
    EXPECT_THROW(parse({"--backtest", "--positions", "KO:ten@58"}), std::invalid_argument);
    EXPECT_THROW(parse({"--backtest", "--positions", "KO:0@58"}), std::invalid_argument);
    EXPECT_THROW(parse({"--positions", "KO:10@58"}), std::invalid_argument);   // live holds what Alpaca holds
}

TEST(LiveOptionsTests, AnotherPortfolioAndRecordsFolderForABacktest)
{
    LiveOptions options = parse({"--backtest", "2026-11-10", "--portfolio", "data/picks/2026-11-07/top_picks_config.json",
                                 "--records", "picks/backtest/2026-11-10"});
    EXPECT_EQ(options.backtestPortfolio, "data/picks/2026-11-07/top_picks_config.json");
    EXPECT_EQ(options.backtestRecords, "picks/backtest/2026-11-10");
    EXPECT_THROW(parse({"--portfolio", "picks.json"}), std::invalid_argument);   // live uses config/strategy.json
    EXPECT_THROW(parse({"--records", "elsewhere"}), std::invalid_argument);
}

TEST(LiveOptionsTests, EveryOption)
{
    LiveOptions options = parse({"--backtest", "--speed", "10", "--quiet", "--verbose"});
    EXPECT_TRUE(options.backtest);
    EXPECT_TRUE(options.backtestDay.empty());   // no day: the latest saved one
    EXPECT_DOUBLE_EQ(options.backtestSpeed, 10);
    EXPECT_TRUE(options.quiet);
    EXPECT_TRUE(options.verbose);

    EXPECT_DOUBLE_EQ(parse({"--backtest", "--speed", "-3"}).backtestSpeed, 0);   // as fast as possible
    EXPECT_TRUE(parse({"--help"}).showHelp);
    EXPECT_THROW(parse({"--bakctest"}), std::invalid_argument);
}

TEST(LiveOptionsTests, UsageListsTheOptions)
{
    std::ostringstream usage;
    LiveOptions::printUsage(usage);
    for (const char* option : {"--backtest", "--days", "--speed", "--capital", "--quiet", "--verbose"})
        EXPECT_NE(usage.str().find(option), std::string::npos) << option;
}
