#include "Format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    const double PERCENT = 100.0;
    const int DIGITS_PER_GROUP = 3;   // 1,234,567

    const long long SECONDS_PER_MINUTE = 60;
    const long long MINUTES_PER_HOUR = 60;
    const long long MINUTES_PER_DAY = 24 * MINUTES_PER_HOUR;

    // "1234567.89" -> "1,234,567.89"
    std::string withThousandsSeparators(std::string digits)
    {
        size_t point = digits.find('.');
        int end = static_cast<int>(point == std::string::npos ? digits.size() : point);
        for (int i = end - DIGITS_PER_GROUP; i > 0; i -= DIGITS_PER_GROUP)
            digits.insert(static_cast<size_t>(i), ",");
        return digits;
    }
}

namespace Format
{
    std::string number(double value, int decimals)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "%.*f", decimals, value);
        return text;
    }

    std::string signedNumber(double value, int decimals)
    {
        // The sign of what's shown: -0.0001 shown to 2 decimals is "+0.00", not "+-0.00" or "-0.00"
        std::string text = number(std::fabs(value), decimals);
        bool showsZero = text.find_first_not_of("0.") == std::string::npos;
        return (value < 0 && !showsZero ? "-" : "+") + text;
    }

    std::string dollars(double value, int decimals)
    {
        return (value < 0 ? "-$" : "$") + withThousandsSeparators(number(std::fabs(value), decimals));
    }

    std::string dollarChange(double value, int decimals)
    {
        std::string amount = number(std::fabs(value), decimals);
        bool showsZero = amount.find_first_not_of("0.") == std::string::npos;
        return (value < 0 && !showsZero ? "-$" : "+$") + withThousandsSeparators(amount);
    }

    std::string percent(double fraction, int decimals)
    {
        return number(fraction * PERCENT, decimals) + "%";
    }

    std::string percentChange(double fraction, int decimals)
    {
        return signedNumber(fraction * PERCENT, decimals) + "%";
    }
}

std::string
Format::duration(long long seconds)
{
    long long minutes = std::max(0LL, seconds) / SECONDS_PER_MINUTE;
    long long days = minutes / MINUTES_PER_DAY, hours = minutes % MINUTES_PER_DAY / MINUTES_PER_HOUR;
    minutes %= MINUTES_PER_HOUR;

    char text[64];
    if (days > 0)
        std::snprintf(text, sizeof(text), "%lldd %lldh %02lldm", days, hours, minutes);
    else if (hours > 0)
        std::snprintf(text, sizeof(text), "%lldh %02lldm", hours, minutes);
    else
        std::snprintf(text, sizeof(text), "%lldm", minutes);
    return text;
}
