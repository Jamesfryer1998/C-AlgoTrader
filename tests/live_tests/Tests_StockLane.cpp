#include <gtest/gtest.h>
#include "../../src/live/StockLane.hpp"
#include "../../src/strategy_engine/Portfolio.hpp"

namespace
{
    const int LOOKBACK = 10;

    // KO with one MEANREV strategy (10 bars, entry at 2 standard deviations), sized 5 shares an order
    PortfolioEntry ko()
    {
        json config = {{"max_position_size", 5}, {"max_exposure", 5.0}, {"slippage_tolerance", 2.0},
                       {"risk", {{"name", "FIXED_SIZE"}, {"quantity", 5}}},
                       {"portfolio", {{{"ticker", "KO"},
                                       {"strategy", {{"name", "MEANREV"}, {"lookback_period", LOOKBACK},
                                                     {"moving_average_period", LOOKBACK}, {"entry_threshold", 2.0},
                                                     {"exit_threshold", 0.0}, {"stop_loss", 1}, {"take_profit", 2}}}}}}};
        return Portfolio::fromConfig(config).front();
    }

    MarketCondition bar(int minute, float close)
    {
        std::string time = "2026-10-26 10:" + std::string(minute < 10 ? "0" : "") + std::to_string(minute) + ":00";
        return MarketCondition(time, "KO", close, close, 1000, "1m");
    }

    // LOOKBACK flat-ish bars: alternating 60.00 / 60.10
    std::vector<MarketCondition> flatHistory()
    {
        std::vector<MarketCondition> bars;
        for (int i = 0; i < LOOKBACK; i++)
            bars.push_back(bar(i, i % 2 ? 60.10f : 60.00f));
        return bars;
    }
}

TEST(StockLaneTests, AsksForTheBarsItsStrategiesNeed)
{
    StockLane lane(ko());
    EXPECT_EQ(lane.getTicker(), "KO");
    EXPECT_EQ(lane.barsNeeded(), LOOKBACK);
    EXPECT_EQ(lane.strategyNames(), "MEANREV");
    ASSERT_EQ(lane.activeStrategies().size(), 1u);
    EXPECT_EQ(lane.activeStrategies()[0]["lookback_period"], LOOKBACK);
    EXPECT_NE(lane.describeRisk().find("FIXED_SIZE"), std::string::npos);
}

TEST(StockLaneTests, BackfillThenOnlyKeepsWhatItNeeds)
{
    StockLane lane(ko());
    lane.backfill(flatHistory());
    EXPECT_EQ(lane.backfilledBars(), static_cast<size_t>(LOOKBACK));

    for (int minute = LOOKBACK; minute < LOOKBACK + 50; minute++)
        lane.addBar(bar(minute % 60, 60.0f));
    // Trimmed in batches: never more than twice what the strategy looks at
    EXPECT_LE(lane.getMarketData().getData().size(), static_cast<size_t>(2 * LOOKBACK));
    EXPECT_GE(lane.getMarketData().getData().size(), static_cast<size_t>(LOOKBACK));
}

TEST(StockLaneTests, DecidesOnTheNewestBarAndSizesTheOrder)
{
    StockLane lane(ko());
    lane.backfill(flatHistory());
    EXPECT_FALSE(lane.decide(0));   // nothing unusual yet

    lane.addBar(bar(LOOKBACK, 58.0f));   // far below the average: buy the dip
    std::optional<Order> order = lane.decide(0);
    ASSERT_TRUE(order);
    EXPECT_TRUE(order->isBuy());
    EXPECT_EQ(order->getTicker(), "KO");

    RiskEngine::SizingContext context;
    context.equity = 100000;
    std::optional<Order> sized = lane.size(*order, context);
    ASSERT_TRUE(sized);
    EXPECT_FLOAT_EQ(sized->getQuantity(), 5);   // FIXED_SIZE quantity

    // FIXED_SIZE never resizes, even when already long: the OMS's max_position_size caps the position
    context.heldQuantity = 5;
    sized = lane.size(*order, context);
    ASSERT_TRUE(sized);
    EXPECT_FLOAT_EQ(sized->getQuantity(), 5);
}
