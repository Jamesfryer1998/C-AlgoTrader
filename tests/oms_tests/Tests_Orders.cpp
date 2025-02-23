#include <gtest/gtest.h>
#include "../../src/oms/Order.hpp"

TEST(OrderTests, CannBeInstantiated)
{
    Order cut{
            1,
            "Filled",
            "AAPL",
            100.0,
            120.0};
}