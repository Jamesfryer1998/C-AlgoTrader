#pragma once

#include <string>

/**
 * Format
 *
 * Text for numbers, money and percentages, shared by every app's printed output:
 *
 *   Format::number(280.1)            "280.10"
 *   Format::signedNumber(0.56)       "+0.56"
 *   Format::dollars(99674.35)        "$99,674.35"
 *   Format::dollars(3101.4, 0)       "$3,101"
 *   Format::dollarChange(-50.67)     "-$50.67"
 *   Format::percent(0.123)           "12.3%"     (takes a fraction)
 *   Format::percentChange(0.123)     "+12.3%"
 */
namespace Format
{
    // A length of time: "45m", "2h 05m", "2d 17h 05m"
    std::string duration(long long seconds);

    std::string number(double value, int decimals = 2);
    std::string signedNumber(double value, int decimals = 2);
    std::string dollars(double value, int decimals = 2);
    std::string dollarChange(double value, int decimals = 2);
    std::string percent(double fraction, int decimals = 1);
    std::string percentChange(double fraction, int decimals = 1);
}
