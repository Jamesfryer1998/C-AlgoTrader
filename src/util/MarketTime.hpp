#pragma once

#include <ctime>
#include <string>

/**
 * MarketTime
 *
 * Small helpers for the two clocks we deal with:
 *   - Alpaca sends UTC timestamps, e.g. "2026-10-29T14:31:00Z"
 *   - Our CSV files and MarketCondition::DateTime use New York exchange time, e.g. "2026-10-29 10:31:00"
 *
 * US daylight saving: starts 2nd Sunday of March 02:00, ends 1st Sunday of November 02:00 (local).
 */
namespace MarketTime
{
    constexpr std::time_t SECONDS_PER_HOUR = 60 * 60;
    constexpr std::time_t SECONDS_PER_DAY = 24 * SECONDS_PER_HOUR;

    // Regular trading hours in New York, as minutes since midnight
    constexpr int OPEN_MINUTE = 9 * 60 + 30;   // 09:30
    constexpr int CLOSE_MINUTE = 16 * 60;      // 16:00
    constexpr int MINUTES_PER_SESSION = CLOSE_MINUTE - OPEN_MINUTE;   // 390
    constexpr int TRADING_DAYS_PER_YEAR = 252;

    constexpr size_t DATE_LENGTH = 10;         // "YYYY-MM-DD"

    // "2026-10-29T14:31:00Z" or "2026-10-29T10:31:00-04:00" (fractional seconds ignored) -> seconds since
    // epoch. Returns -1 if unparseable.
    std::time_t parseUtc(const std::string& rfc3339);

    // "2026-10-29" -> midnight UTC of that date. Returns -1 if unparseable.
    std::time_t parseDate(const std::string& ymd);

    // Seconds since epoch -> "2026-10-29T14:31:00Z"
    std::string formatUtc(std::time_t utc);

    // Is New York on daylight saving time (EDT, UTC-4) at this moment? Otherwise EST, UTC-5.
    bool isNewYorkDst(std::time_t utc);

    // Seconds since epoch -> New York local time "2026-10-29 10:31:00"
    std::string toNewYorkTime(std::time_t utc);

    // "YYYY-MM-DD" of a "YYYY-MM-DD HH:MM:SS" string
    std::string datePart(const std::string& dateTime);

    // "YYYY-MM-01" of the month a "YYYY-MM-DD..." falls in
    std::string firstOfMonth(const std::string& date);

    // "YYYY-MM-DD" moved back by whole months (day capped at 28 so it always exists)
    std::string monthsBefore(const std::string& date, int months);

    // When regular hours next open, and when they close after that (or today's close if open now).
    // Weekdays only: market holidays aren't known here (Alpaca's clock knows them; this is the fallback).
    std::time_t nextRegularOpen(std::time_t utc);
    std::time_t regularCloseAfter(std::time_t utc);

    // "HH:MM" of a "YYYY-MM-DD HH:MM:SS" as minutes since midnight (-1 if unreadable)
    int minuteOfDay(const std::string& dateTime);

    // Minutes since midnight -> "HH:MM"
    std::string clock(int minute);

    // Is a New York "YYYY-MM-DD HH:MM:SS" inside regular hours 09:30-15:59?
    bool isRegularSession(const std::string& newYorkDateTime);
}
