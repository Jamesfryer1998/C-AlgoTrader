#include <gtest/gtest.h>
#include "../../src/oms/Position.hpp"

TEST(PositionTests, CannBeInstantiated)
{
    Position cut{
        1,
        "AAPL",
        100.0,
        120.0
    };
}