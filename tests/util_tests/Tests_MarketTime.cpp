#include <gtest/gtest.h>
#include "../../src/util/MarketTime.hpp"

using namespace MarketTime;

TEST(MarketTimeTests, ParseAndFormatUtcRoundTrip)
{
    std::time_t t = parseUtc("2026-10-29T14:31:00Z");
    ASSERT_GT(t, 0);
    EXPECT_EQ(formatUtc(t), "2026-10-29T14:31:00Z");
    EXPECT_EQ(parseUtc("2026-10-29T14:31:00.123456Z"), t); // fractional seconds ignored
    EXPECT_EQ(parseUtc("garbage"), -1);
}

TEST(MarketTimeTests, SummerIsUtcMinus4)
{
    EXPECT_EQ(toNewYorkTime(parseUtc("2026-07-15T13:30:00Z")), "2026-07-15 09:30:00");
}

TEST(MarketTimeTests, WinterIsUtcMinus5)
{
    EXPECT_EQ(toNewYorkTime(parseUtc("2026-01-15T14:30:00Z")), "2026-01-15 09:30:00");
}

TEST(MarketTimeTests, LateOctoberUkHasChangedButUsHasNot)
{
    // UK clocks went back 25 Oct 2026, US clocks go back 1 Nov 2026: NY still UTC-4
    EXPECT_EQ(toNewYorkTime(parseUtc("2026-10-29T13:30:00Z")), "2026-10-29 09:30:00");
}

TEST(MarketTimeTests, DstStartsSecondSundayOfMarch)
{
    // 2026-03-08 is the 2nd Sunday. 02:00 EST = 07:00 UTC
    EXPECT_FALSE(isNewYorkDst(parseUtc("2026-03-08T06:59:59Z")));
    EXPECT_TRUE(isNewYorkDst(parseUtc("2026-03-08T07:00:00Z")));
}

TEST(MarketTimeTests, DstEndsFirstSundayOfNovember)
{
    // 2026-11-01 is the 1st Sunday. 02:00 EDT = 06:00 UTC
    EXPECT_TRUE(isNewYorkDst(parseUtc("2026-11-01T05:59:59Z")));
    EXPECT_FALSE(isNewYorkDst(parseUtc("2026-11-01T06:00:00Z")));
}

TEST(MarketTimeTests, RegularSession)
{
    EXPECT_FALSE(isRegularSession("2026-10-29 09:29:00"));
    EXPECT_TRUE(isRegularSession("2026-10-29 09:30:00"));
    EXPECT_TRUE(isRegularSession("2026-10-29 15:59:00"));
    EXPECT_FALSE(isRegularSession("2026-10-29 16:00:00"));
}

TEST(MarketTimeTests, DatePartAndParseDate)
{
    EXPECT_EQ(datePart("2026-10-29 09:30:00"), "2026-10-29");
    EXPECT_EQ(formatUtc(parseDate("2026-10-29")), "2026-10-29T00:00:00Z");
}

TEST(MarketTimeTests, MinuteOfDayAndClockAreOpposites)
{
    EXPECT_EQ(MarketTime::minuteOfDay("2026-10-29 09:30:00"), MarketTime::OPEN_MINUTE);
    EXPECT_EQ(MarketTime::minuteOfDay("2026-10-29 16:00:00"), MarketTime::CLOSE_MINUTE);
    EXPECT_EQ(MarketTime::minuteOfDay("2026-10-29"), -1);
    EXPECT_EQ(MarketTime::clock(MarketTime::OPEN_MINUTE), "09:30");
}

TEST(MarketTimeTests, MonthsBeforeCrossesYearsAndShortMonths)
{
    EXPECT_EQ(MarketTime::monthsBefore("2026-11-05", 6), "2026-05-05");
    EXPECT_EQ(MarketTime::monthsBefore("2026-02-10", 24), "2024-02-10");
    EXPECT_EQ(MarketTime::monthsBefore("2026-03-31", 1), "2026-02-28");
}

TEST(MarketTimeTests, ParsesUtcOffsets)
{
    // Alpaca's clock: New York time with its offset
    EXPECT_EQ(MarketTime::parseUtc("2026-11-06T09:30:00-05:00"), MarketTime::parseUtc("2026-11-06T14:30:00Z"));
    EXPECT_EQ(MarketTime::parseUtc("2026-11-06T08:34:12.123-05:00"), MarketTime::parseUtc("2026-11-06T13:34:12Z"));
    EXPECT_EQ(MarketTime::parseUtc("2026-07-01T01:00:00+01:00"), MarketTime::parseUtc("2026-07-01T00:00:00Z"));
}

TEST(MarketTimeTests, NextRegularOpenSkipsWeekends)
{
    // Friday 13:34 UK (08:34 New York, EST): opens today at 09:30 New York = 14:30 UTC
    std::time_t fridayMorning = MarketTime::parseUtc("2026-11-06T13:34:00Z");
    EXPECT_EQ(MarketTime::nextRegularOpen(fridayMorning), MarketTime::parseUtc("2026-11-06T14:30:00Z"));
    EXPECT_EQ(MarketTime::regularCloseAfter(fridayMorning), MarketTime::parseUtc("2026-11-06T21:00:00Z"));

    // Friday after the close: Monday 09:30
    std::time_t fridayEvening = MarketTime::parseUtc("2026-11-06T22:00:00Z");
    EXPECT_EQ(MarketTime::nextRegularOpen(fridayEvening), MarketTime::parseUtc("2026-11-09T14:30:00Z"));

    // Summer (EDT): 09:30 New York = 13:30 UTC
    std::time_t julyMonday = MarketTime::parseUtc("2026-07-06T12:00:00Z");
    EXPECT_EQ(MarketTime::nextRegularOpen(julyMonday), MarketTime::parseUtc("2026-07-06T13:30:00Z"));
}

TEST(MarketTimeTests, WeekdaysSkipWeekends)
{
    EXPECT_TRUE(MarketTime::isWeekday("2026-11-06"));    // Friday
    EXPECT_FALSE(MarketTime::isWeekday("2026-11-07"));   // Saturday
    EXPECT_EQ(MarketTime::addWeekdays("2026-11-06", 1), "2026-11-09");
    EXPECT_EQ(MarketTime::addWeekdays("2026-11-09", -1), "2026-11-06");
    EXPECT_EQ(MarketTime::addWeekdays("2026-10-15", -5), "2026-10-08");
    EXPECT_EQ(MarketTime::addWeekdays("2026-10-15", 0), "2026-10-15");
    EXPECT_EQ(MarketTime::weekdaysBetween("2026-11-06", "2026-11-10"),
              (std::vector<std::string>{"2026-11-06", "2026-11-09", "2026-11-10"}));
}
