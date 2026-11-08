#include <gtest/gtest.h>
#include "../../src/util/Format.hpp"

TEST(FormatTests, DurationShowsTheLargestUnitsNeeded)
{
    EXPECT_EQ(Format::duration(45 * 60 + 59), "45m");
    EXPECT_EQ(Format::duration(2 * 3600 + 5 * 60), "2h 05m");
    EXPECT_EQ(Format::duration(2 * 86400 + 17 * 3600 + 5 * 60), "2d 17h 05m");
    EXPECT_EQ(Format::duration(-10), "0m");
}

TEST(FormatTests, MoneyAndPercent)
{
    EXPECT_EQ(Format::dollars(100009), "$100,009.00");
    EXPECT_EQ(Format::dollarChange(-9.5), "-$9.50");
    EXPECT_EQ(Format::percentChange(0.0001, 2), "+0.01%");
}

TEST(FormatTests, ATinyLossRoundedToZeroShowsAsZero)
{
    // A position that has hardly moved: no "+-0.00%" or "-$0.00"
    EXPECT_EQ(Format::percentChange(-0.0000001, 2), "+0.00%");
    EXPECT_EQ(Format::percentChange(-0.0, 2), "+0.00%");
    EXPECT_EQ(Format::signedNumber(-0.001, 2), "+0.00");
    EXPECT_EQ(Format::dollarChange(-0.001), "+$0.00");
    // Real losses keep their sign
    EXPECT_EQ(Format::percentChange(-0.0001, 2), "-0.01%");
    EXPECT_EQ(Format::signedNumber(-1.5, 1), "-1.5");
    EXPECT_EQ(Format::dollarChange(-1234.5), "-$1,234.50");
}
