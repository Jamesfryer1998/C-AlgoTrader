#include <gtest/gtest.h>
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
