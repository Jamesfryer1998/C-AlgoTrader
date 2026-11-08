#include <gtest/gtest.h>
#include "../../src/live/CapitalUsage.hpp"

TEST(CapitalUsageTests, PeakIsTheMostHeldAtOnce)
{
    CapitalUsage capital;
    capital.update("AAA", 10, 100, "2026-10-01 09:31:00");   // $1,000
    capital.update("BBB", -5, 200, "2026-10-01 09:31:00");   // short $1,000: $2,000 in use
    capital.update("AAA", 0, 101, "2026-10-01 09:32:00");    // AAA closed: $1,000
    capital.update("BBB", -5, 150, "2026-10-01 09:32:00");   // $750

    EXPECT_DOUBLE_EQ(capital.peak(), 2000);
    EXPECT_EQ(capital.peakTime(), "2026-10-01 09:31:00");
    EXPECT_EQ(capital.describe(100000), "Most capital in use: $2,000.00 (2.00% of $100,000.00), at 2026-10-01 09:31 New York");
}

TEST(CapitalUsageTests, NoPositions)
{
    CapitalUsage capital;
    capital.update("AAA", 0, 100, "2026-10-01 09:31:00");
    EXPECT_EQ(capital.describe(100000), "Most capital in use: $0 (no positions were opened)");
}
