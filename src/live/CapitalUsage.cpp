#include "CapitalUsage.hpp"

#include <cmath>
#include "../util/Format.hpp"

namespace
{
    const size_t TIME_WITHOUT_SECONDS = 16;   // "2026-10-01 11:42:00" -> "2026-10-01 11:42"
    const int SHARE_DECIMALS = 2;
}

void
CapitalUsage::update(const std::string& ticker, double quantity, double price, const std::string& dateTime)
{
    valueByTicker[ticker] = std::fabs(quantity) * price;

    double total = 0;
    for (const auto& [name, value] : valueByTicker)
        total += value;
    if (total > peakValue)
    {
        peakValue = total;
        peakAt = dateTime;
    }
}

std::string
CapitalUsage::describe(double startingCapital) const
{
    if (peakValue <= 0)
        return "Most capital in use: $0 (no positions were opened)";
    std::string text = "Most capital in use: " + Format::dollars(peakValue);
    if (startingCapital > 0)
        text += " (" + Format::percent(peakValue / startingCapital, SHARE_DECIMALS) + " of " + Format::dollars(startingCapital) + ")";
    return text + ", at " + peakAt.substr(0, TIME_WITHOUT_SECONDS) + " New York";
}
