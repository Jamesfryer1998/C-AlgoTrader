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
    // "2026-10-29T14:31:00Z" (fractional seconds ignored) -> seconds since epoch. Returns -1 if unparseable.
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

    // Is a New York "YYYY-MM-DD HH:MM:SS" inside regular hours 09:30-15:59?
    bool isRegularSession(const std::string& newYorkDateTime);
}
