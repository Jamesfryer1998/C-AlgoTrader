#include "MarketTime.hpp"

#include <cstdio>
#include <iomanip>
#include <sstream>

namespace
{
    std::time_t makeUtc(int year, int month, int day, int hour = 0, int minute = 0, int second = 0)
    {
        std::tm tm{};
        tm.tm_year = year - 1900;
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
        int firstSunday = 1 + (7 - tm.tm_wday) % 7;
        return firstSunday + 7 * (n - 1);
    }
}

std::time_t
MarketTime::parseUtc(const std::string& rfc3339)
{
    if (rfc3339.size() < 19)
        return -1;

    std::tm tm{};
    std::istringstream in(rfc3339.substr(0, 19));
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (in.fail())
        return -1;
    return timegm(&tm);
}

std::time_t
MarketTime::parseDate(const std::string& ymd)
{
    std::tm tm{};
    std::istringstream in(ymd);
    in >> std::get_time(&tm, "%Y-%m-%d");
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
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return text;
}

bool
MarketTime::isNewYorkDst(std::time_t utc)
{
    std::tm tm{};
    gmtime_r(&utc, &tm);
    int year = tm.tm_year + 1900;

    // 02:00 EST = 07:00 UTC on the 2nd Sunday of March; 02:00 EDT = 06:00 UTC on the 1st Sunday of November
    std::time_t dstStart = makeUtc(year, 3, nthSunday(year, 3, 2), 7);
    std::time_t dstEnd = makeUtc(year, 11, nthSunday(year, 11, 1), 6);
    return utc >= dstStart && utc < dstEnd;
}

std::string
MarketTime::toNewYorkTime(std::time_t utc)
{
    std::time_t local = utc - (isNewYorkDst(utc) ? 4 : 5) * 3600;
    std::tm tm{};
    gmtime_r(&local, &tm);
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &tm);
    return text;
}

std::string
MarketTime::datePart(const std::string& dateTime)
{
    return dateTime.substr(0, 10);
}

bool
MarketTime::isRegularSession(const std::string& newYorkDateTime)
{
    if (newYorkDateTime.size() < 16)
        return false;
    std::string hhmm = newYorkDateTime.substr(11, 5); // "HH:MM"
    return hhmm >= "09:30" && hhmm < "16:00";
}
