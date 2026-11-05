#include "MarketTime.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <iomanip>
#include <sstream>

namespace
{
    const int TM_FIRST_YEAR = 1900;   // std::tm counts years from 1900
    const int DAYS_PER_WEEK = 7;
    const int MONTHS_PER_YEAR = 12;
    const int MINUTES_PER_HOUR = 60;
    const int HOURS_PER_DAY = 24;

    const char* const UTC_FORMAT = "%Y-%m-%dT%H:%M:%SZ";        // 2026-10-29T14:31:00Z
    const char* const UTC_FORMAT_NO_ZONE = "%Y-%m-%dT%H:%M:%S";
    const size_t UTC_LENGTH_NO_ZONE = 19;                        // 2026-10-29T14:31:00
    const char* const DATE_FORMAT = "%Y-%m-%d";                  // 2026-10-29
    const char* const DATE_TIME_FORMAT = "%Y-%m-%d %H:%M:%S";    // 2026-10-29 10:31:00

    // New York: EST (UTC-5) in winter, EDT (UTC-4) from 02:00 on the 2nd Sunday of March
    // (07:00 UTC) to 02:00 on the 1st Sunday of November (06:00 UTC)
    const int WINTER_HOURS_BEHIND_UTC = 5;
    const int SUMMER_HOURS_BEHIND_UTC = 4;
    const int DST_START_MONTH = 3, DST_START_SUNDAY = 2, DST_START_HOUR_UTC = 7;
    const int DST_END_MONTH = 11, DST_END_SUNDAY = 1, DST_END_HOUR_UTC = 6;

    const size_t MONTH_LENGTH = 7;            // "YYYY-MM"
    const std::string FIRST_DAY = "-01";

    // monthsBefore keeps the day of the month, capped so it exists in every month
    const int LAST_DAY_IN_EVERY_MONTH = 28;

    std::time_t makeUtc(int year, int month, int day, int hour = 0, int minute = 0, int second = 0)
    {
        std::tm tm{};
        tm.tm_year = year - TM_FIRST_YEAR;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = second;
        return timegm(&tm);
    }

    // Day of month of the n-th Sunday (n = 1, 2, ...) of a month
    int nthSunday(int year, int month, int n)
    {
        std::time_t first = makeUtc(year, month, 1);
        std::tm tm{};
        gmtime_r(&first, &tm);
        int firstSunday = 1 + (DAYS_PER_WEEK - tm.tm_wday) % DAYS_PER_WEEK;
        return firstSunday + DAYS_PER_WEEK * (n - 1);
    }
}

std::time_t
MarketTime::parseUtc(const std::string& rfc3339)
{
    if (rfc3339.size() < UTC_LENGTH_NO_ZONE)
        return -1;

    std::tm tm{};
    std::istringstream in(rfc3339.substr(0, UTC_LENGTH_NO_ZONE));
    in >> std::get_time(&tm, UTC_FORMAT_NO_ZONE);
    if (in.fail())
        return -1;
    return timegm(&tm);
}

std::time_t
MarketTime::parseDate(const std::string& ymd)
{
    std::tm tm{};
    std::istringstream in(ymd);
    in >> std::get_time(&tm, DATE_FORMAT);
    if (in.fail())
        return -1;
    return timegm(&tm);
}

std::string
MarketTime::formatUtc(std::time_t utc)
{
    std::tm tm{};
    gmtime_r(&utc, &tm);
    char text[32];
    std::strftime(text, sizeof(text), UTC_FORMAT, &tm);
    return text;
}

bool
MarketTime::isNewYorkDst(std::time_t utc)
{
    std::tm tm{};
    gmtime_r(&utc, &tm);
    int year = tm.tm_year + TM_FIRST_YEAR;

    std::time_t dstStart = makeUtc(year, DST_START_MONTH, nthSunday(year, DST_START_MONTH, DST_START_SUNDAY), DST_START_HOUR_UTC);
    std::time_t dstEnd = makeUtc(year, DST_END_MONTH, nthSunday(year, DST_END_MONTH, DST_END_SUNDAY), DST_END_HOUR_UTC);
    return utc >= dstStart && utc < dstEnd;
}

std::string
MarketTime::toNewYorkTime(std::time_t utc)
{
    std::time_t local = utc - (isNewYorkDst(utc) ? SUMMER_HOURS_BEHIND_UTC : WINTER_HOURS_BEHIND_UTC) * SECONDS_PER_HOUR;
    std::tm tm{};
    gmtime_r(&local, &tm);
    char text[32];
    std::strftime(text, sizeof(text), DATE_TIME_FORMAT, &tm);
    return text;
}

std::string
MarketTime::datePart(const std::string& dateTime)
{
    return dateTime.substr(0, DATE_LENGTH);
}

std::string
MarketTime::firstOfMonth(const std::string& date)
{
    return date.substr(0, MONTH_LENGTH) + FIRST_DAY;
}

std::string
MarketTime::monthsBefore(const std::string& date, int months)
{
    int year = std::stoi(date.substr(0, 4)), month = std::stoi(date.substr(5, 2)), day = std::stoi(date.substr(8, 2));
    month -= months;
    while (month <= 0)
    {
        month += MONTHS_PER_YEAR;
        year--;
    }
    char text[48];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d", year, month, std::min(day, LAST_DAY_IN_EVERY_MONTH));
    return text;
}

int
MarketTime::minuteOfDay(const std::string& dateTime)
{
    // "YYYY-MM-DD HH:MM..."
    const size_t hourAt = DATE_LENGTH + 1, minuteAt = hourAt + 3;
    if (dateTime.size() < minuteAt + 2)
        return -1;
    try
    {
        return std::stoi(dateTime.substr(hourAt, 2)) * MINUTES_PER_HOUR + std::stoi(dateTime.substr(minuteAt, 2));
    }
    catch (const std::exception&)
    {
        return -1;
    }
}

std::string
MarketTime::clock(int minute)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%02d:%02d", (minute / MINUTES_PER_HOUR) % HOURS_PER_DAY, minute % MINUTES_PER_HOUR);
    return text;
}

bool
MarketTime::isRegularSession(const std::string& newYorkDateTime)
{
    int minute = minuteOfDay(newYorkDateTime);
    return minute >= OPEN_MINUTE && minute < CLOSE_MINUTE;
}
