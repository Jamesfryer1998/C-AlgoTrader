#include "Format.hpp"

#include <cmath>
#include <cstdio>

namespace
{
    const double PERCENT = 100.0;
    const int DIGITS_PER_GROUP = 3;   // 1,234,567

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
        return (value >= 0 ? "+" : "") + number(value, decimals);
    }

    std::string dollars(double value, int decimals)
    {
        return (value < 0 ? "-$" : "$") + withThousandsSeparators(number(std::fabs(value), decimals));
    }

    std::string dollarChange(double value, int decimals)
    {
        return (value < 0 ? "-$" : "+$") + withThousandsSeparators(number(std::fabs(value), decimals));
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
