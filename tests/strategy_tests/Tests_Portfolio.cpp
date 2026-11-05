#include <gtest/gtest.h>
#include "../../src/strategy_engine/Portfolio.hpp"

namespace
{
    json baseConfig()
    {
        return {{"ticker", "NVDA"}, {"max_position_size", 10},
                {"strategies", {{{"name", "RSI"}, {"active", 1}, {"period", 21}}}}};
    }
}

TEST(PortfolioTests, NoPortfolioMeansTheSingleTopLevelTicker)
{
    auto entries = Portfolio::fromConfig(baseConfig());
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].ticker, "NVDA");
    EXPECT_EQ(entries[0].config["strategies"][0]["period"], 21);

    json empty = baseConfig();
    empty["portfolio"] = json::array();
    EXPECT_EQ(Portfolio::fromConfig(empty)[0].ticker, "NVDA");
}

TEST(PortfolioTests, ReadsTheScreensTopPicksFormat)
{
    // Exactly what screen_app writes to data/screen/minute/top_picks_config.json
    json config = baseConfig();
    config["portfolio"] = json::parse(R"([
        {"strategy": {"active": 1, "entry_threshold": 3.0, "exit_threshold": 0.0, "lookback_period": 60,
                      "moving_average_period": 60, "name": "MEANREV", "stop_loss": 1, "take_profit": 2},
         "ticker": "TXN"},
        {"strategy": {"name": "RSI", "overbought_threshold": 80, "oversold_threshold": 25, "period": 45,
                      "stop_loss": 5, "take_profit": 5},
         "ticker": "WMT"}
    ])");

    auto entries = Portfolio::fromConfig(config);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].ticker, "TXN");
    EXPECT_EQ(entries[0].config["ticker"], "TXN");
    ASSERT_EQ(entries[0].config["strategies"].size(), 1u);
    EXPECT_EQ(entries[0].config["strategies"][0]["name"], "MEANREV");
    EXPECT_EQ(entries[1].config["strategies"][0]["active"], 1);   // missing "active" means on
    EXPECT_EQ(entries[1].config["max_position_size"], 10);        // risk settings carried over
    EXPECT_FALSE(entries[1].config.contains("portfolio"));
}

TEST(PortfolioTests, SeveralStrategiesForOneStock)
{
    json config = baseConfig();
    config["portfolio"] = {{{"ticker", "KO"}, {"strategies", {{{"name", "RSI"}}, {{"name", "MACD"}, {"active", 0}}}}}};
    auto entries = Portfolio::fromConfig(config);
    ASSERT_EQ(entries.size(), 1u);
    ASSERT_EQ(entries[0].config["strategies"].size(), 2u);
    EXPECT_EQ(entries[0].config["strategies"][1]["active"], 0);   // kept as given
}

TEST(PortfolioTests, BadEntriesAreRejectedClearly)
{
    auto withPortfolio = [](json portfolio) {
        json config = baseConfig();
        config["portfolio"] = portfolio;
        return config;
    };
    EXPECT_THROW(Portfolio::fromConfig(withPortfolio({{{"strategy", {{"name", "RSI"}}}}})), std::runtime_error);
    EXPECT_THROW(Portfolio::fromConfig(withPortfolio({{{"ticker", "KO"}}})), std::runtime_error);
    EXPECT_THROW(Portfolio::fromConfig(withPortfolio({{{"ticker", "KO"}, {"strategy", {{"period", 3}}}}})), std::runtime_error);
    EXPECT_THROW(Portfolio::fromConfig(withPortfolio({{{"ticker", "KO"}, {"strategy", {{"name", "RSI"}}}},
                                                      {{"ticker", "KO"}, {"strategy", {{"name", "RSI"}}}}})),
                 std::runtime_error);
}
