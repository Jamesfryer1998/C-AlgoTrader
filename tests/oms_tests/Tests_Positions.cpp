#include <gtest/gtest.h>
#include "../../src/oms/Position.hpp"

TEST(PositionTests, CanBeInstantiated)
{
    Position cut{
        "AAPL",
        100.0,
        120.0
    };
}

TEST(PositionTests, CanSetAndGetValues)
{
    Position cut{
        "AAPL",
        100.0,
        120.0
    };

    cut.setId(1);
    cut.setTicker("GOOG");
    cut.setQuantity(50.0);
    
    EXPECT_EQ(cut.getId(), 1);
    EXPECT_EQ(cut.getTicker(), "GOOG");
    EXPECT_EQ(cut.getQuantity(), 50.0);
}