#include <gtest/gtest.h>
#include <sstream>
#include "../../src/live/StartupSummary.hpp"

TEST(StartupSummaryTests, RiskShowsTheEngineAndItsSettings)
{
    json risk = {{"name", "KELLY"}, {"kelly_fraction", 0.5}, {"min_trades", 20}};
    EXPECT_EQ(StartupSummary::describeRisk(risk), "KELLY: kelly_fraction 0.5, min_trades 20");
    EXPECT_EQ(StartupSummary::describeRisk({{"name", "FIXED_SIZE"}}), "FIXED_SIZE");
}

TEST(StartupSummaryTests, SettingsLeaveOutTheColumnsOfTheirOwn)
{
    json meanrev = {{"name", "MEANREV"}, {"active", 1}, {"stop_loss", 1}, {"take_profit", 2},
                    {"entry_threshold", 3.0}, {"lookback_period", 60}};
    EXPECT_EQ(StartupSummary::describeSettings(meanrev), "entry_threshold 3, lookback_period 60");
}

TEST(StartupSummaryTests, PrintsTheHeaderAndOneRowPerStrategy)
{
    StartupSummary summary;
    summary.add("Mode", "LIVE on Alpaca's paper account");
    summary.add("Market", "");   // nothing to say: no line
    summary.addStock("TXN", json::array({{{"name", "MEANREV"}, {"stop_loss", 1}, {"take_profit", 2}, {"lookback_period", 60}}}),
                     60, 60);
    summary.addStock("WMT", json::array({{{"name", "RSI"}, {"period", 45}}, {{"name", "MACD"}}}), 30, 45);

    std::ostringstream out;
    summary.print(out);
    std::string text = out.str();

    EXPECT_NE(text.find(" AlgoTrader "), std::string::npos);
    EXPECT_NE(text.find("Mode      LIVE on Alpaca's paper account"), std::string::npos);
    EXPECT_EQ(text.find("Market"), std::string::npos);
    EXPECT_NE(text.find("Stock  Strategy  Stop  Target  History  Settings"), std::string::npos);
    EXPECT_NE(text.find("TXN    MEANREV     1%      2%    60/60  lookback_period 60"), std::string::npos);
    EXPECT_NE(text.find("WMT    RSI          -       -    30/45  period 45"), std::string::npos);
    EXPECT_NE(text.find("       MACD         -       -           "), std::string::npos);   // the same stock's second strategy
}
