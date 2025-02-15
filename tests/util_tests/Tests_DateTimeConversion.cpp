#include <gtest/gtest.h>
#include "../../src/util/DateTimeConversion.hpp"

TEST(DateTimeConversion, CannBeInstantiated)
{
    DateTimeConversion cut;
}

TEST(DateTimeConversion, ReturnsCorrectEpochTime)
{
    DateTimeConversion cut;
    std::time_t epochTime = 1700000000;
    cut.setTime(epochTime);

    std::tm* tmStruct = cut.getTime();
    std::time_t convertedEpochTime = std::mktime(tmStruct);

    EXPECT_EQ(epochTime, convertedEpochTime);
}

TEST(DateTimeConversion, ReturnsCorrectDateStringEpoch0)
{
    DateTimeConversion cut;
    // 1970-01-01 epoch date
    std::time_t epochTime = 0;
    cut.setTime(epochTime);

    EXPECT_EQ("1970-01-01", cut.timeNowToDate());
}

TEST(DateTimeConversion, ReturnsCorrectDateStringFor2025)
{
    DateTimeConversion cut;
    // 2025-06-15 epoch date
    std::time_t epochTime = 1750000000;
    cut.setTime(epochTime);

    EXPECT_EQ("2025-06-15", cut.timeNowToDate());
}

TEST(DateTimeConversion, ReturnsCorrectStringTimeEpoch0)
{
    DateTimeConversion cut;
    // 2025-06-15 15:06:40 epoch time
    std::time_t epochTime = 0;
    cut.setTime(epochTime);

    EXPECT_EQ("1970-01-01 00:00:00", cut.timeNowToString());
}

TEST(DateTimeConversion, ReturnsCorrectStringTime)
{
    DateTimeConversion cut;
    // 2025-06-15 15:06:40 epoch time
    std::time_t epochTime = 1750000000;
    cut.setTime(epochTime);

    EXPECT_EQ("2025-06-15 15:06:40", cut.timeNowToString());
}

TEST(DateTimeConversion, HandlesNegativeEpochTime_Before1970)
{
    DateTimeConversion cut;
    // December 31, 1969 23:59:59 UTC (Epoch -1)
    std::time_t epochTime = -1;
    cut.setTime(epochTime);

    EXPECT_EQ("1969-12-31", cut.timeNowToDate());
    EXPECT_EQ("1969-12-31 23:59:59", cut.timeNowToString());
}

TEST(DateTimeConversion, HandlesLeapYear_Feb29_2024)
{
    DateTimeConversion cut;
    // February 29, 2024 (Leap year)
    std::time_t epochTime = 1709164800;  // 2024-02-29 00:00:00 UTC
    cut.setTime(epochTime);

    EXPECT_EQ("2024-02-29", cut.timeNowToDate());
}

TEST(DateTimeConversion, HandlesEndOfYearBoundary)
{
    DateTimeConversion cut;
    // December 31, 2024 23:59:59 UTC
    std::time_t epochTime = 1735689599;
    cut.setTime(epochTime);

    EXPECT_EQ("2024-12-31", cut.timeNowToDate());
    EXPECT_EQ("2024-12-31 23:59:59", cut.timeNowToString());
}

TEST(DateTimeConversion, HandlesStartOfYearBoundary)
{
    DateTimeConversion cut;
    // January 1, 2025 00:00:00 UTC
    std::time_t epochTime = 1735689600;
    cut.setTime(epochTime);

    EXPECT_EQ("2025-01-01", cut.timeNowToDate());
    EXPECT_EQ("2025-01-01 00:00:00", cut.timeNowToString());
}

TEST(DateTimeConversion, HandlesFarFutureDate_Year2100)
{
    DateTimeConversion cut;
    // January 1, 2100 00:00:00 UTC
    std::time_t epochTime = 4102444800;
    cut.setTime(epochTime);

    EXPECT_EQ("2100-01-01", cut.timeNowToDate());
}

TEST(DateTimeConversion, HandlesFarPastDate_Year1900)
{
    DateTimeConversion cut;
    // January 1, 1900 00:00:00 UTC
    std::time_t epochTime = -2208988800;
    cut.setTime(epochTime);

    EXPECT_EQ("1900-01-01", cut.timeNowToDate());
}
