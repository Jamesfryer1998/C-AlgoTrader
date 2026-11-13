#include <gtest/gtest.h>
#include "risk/DayEnd.hpp"
#include "util/MarketTime.hpp"

namespace
{
    const json FLAT_AT_CLOSE = {{"end_of_day", {{"flat_at_close", true}, {"no_entries_minutes_before_close", 15},
                                                 {"close_minutes_before_close", 5}}}};
    const int HALF_DAY_CLOSE = 13 * 60;
}

TEST(DayEndTests, OffUnlessTheConfigTurnsItOn)
{
    DayEnd off(json::object());
    EXPECT_FALSE(off.isOn());
    EXPECT_EQ(off.phaseOfBar("2026-11-12 15:58:00"), DayEnd::Phase::Trading);
    EXPECT_FALSE(DayEnd({{"end_of_day", {{"flat_at_close", false}}}}).isOn());
}

TEST(DayEndTests, NoNewPositionsFrom1545AndFlatFrom1555)
{
    DayEnd rule(FLAT_AT_CLOSE);
    EXPECT_EQ(rule.phaseOfBar("2026-11-12 10:00:00"), DayEnd::Phase::Trading);
    EXPECT_EQ(rule.phaseOfBar("2026-11-12 15:43:00"), DayEnd::Phase::Trading);       // ends 15:44
    EXPECT_EQ(rule.phaseOfBar("2026-11-12 15:44:00"), DayEnd::Phase::NoNewEntries);  // ends 15:45
    EXPECT_EQ(rule.phaseOfBar("2026-11-12 15:53:00"), DayEnd::Phase::NoNewEntries);
    EXPECT_EQ(rule.phaseOfBar("2026-11-12 15:54:00"), DayEnd::Phase::Close);         // ends 15:55
    EXPECT_EQ(rule.phaseOfBar("2026-11-12 15:59:00"), DayEnd::Phase::Close);
}

TEST(DayEndTests, AHalfDayClosesEarlier)
{
    DayEnd rule(FLAT_AT_CLOSE);
    EXPECT_EQ(rule.phaseOfBar("2026-11-27 12:46:00", HALF_DAY_CLOSE), DayEnd::Phase::NoNewEntries);
    EXPECT_EQ(rule.phaseOfBar("2026-11-27 12:55:00", HALF_DAY_CLOSE), DayEnd::Phase::Close);
    EXPECT_EQ(rule.phaseAt(12 * 60 + 55, HALF_DAY_CLOSE), DayEnd::Phase::Close);     // the live clock's minute
}

TEST(DayEndTests, DailyBarsAndExtendedHoursAreLeftAlone)
{
    DayEnd rule(FLAT_AT_CLOSE);
    EXPECT_EQ(rule.phaseOfBar("2026-11-12"), DayEnd::Phase::Trading);
    EXPECT_EQ(rule.phaseOfBar("2026-11-12 00:00:00"), DayEnd::Phase::Trading);
    EXPECT_EQ(rule.phaseOfBar("2026-11-12 16:30:00"), DayEnd::Phase::Trading);
}

TEST(DayEndTests, OnlyEntriesAreHeldBack)
{
    Order buy(OrderType::BUY, "KO", 10, 70), sell(OrderType::SELL, "KO", 10, 70);
    EXPECT_TRUE(DayEnd::opensOrAdds(buy, 0));
    EXPECT_TRUE(DayEnd::opensOrAdds(buy, 5));
    EXPECT_FALSE(DayEnd::opensOrAdds(buy, -10));    // covers a short
    EXPECT_FALSE(DayEnd::opensOrAdds(sell, 10));    // sells what we hold
    EXPECT_TRUE(DayEnd::opensOrAdds(sell, 0));      // would open a short
}

TEST(DayEndTests, TheClosingOrderClosesTheWholePosition)
{
    Order closeLong = DayEnd::closingOrder("KO", 12, 70);
    EXPECT_TRUE(closeLong.isSell());
    EXPECT_FLOAT_EQ(closeLong.getQuantity(), 12);
    EXPECT_EQ(closeLong.getStrategy(), DayEnd::STRATEGY);
    Order closeShort = DayEnd::closingOrder("KO", -3, 70);
    EXPECT_TRUE(closeShort.isBuy());
    EXPECT_FLOAT_EQ(closeShort.getQuantity(), 3);
}
