#include "EquityStats.hpp"

#include <algorithm>
#include <cmath>
#include "../util/MarketTime.hpp"

void
EquityStats::update(double equity, const std::string& dateTime)
{
    if (equity <= 0)
        return;
    if (firstEquity <= 0)
        firstEquity = equity;

    highest = std::max(highest, equity);
    worstDrawdown = std::max(worstDrawdown, 1.0 - equity / highest);

    std::string day = MarketTime::datePart(dateTime);
    if (day != currentDay)
    {
        currentDay = day;
        dayCloses.push_back(equity);
    }
    else
        dayCloses.back() = equity;
}

double
EquityStats::sharpe() const
{
    if (dayCloses.size() < 2)
        return 0;
    std::vector<double> returns;
    double previous = firstEquity;
    for (double close : dayCloses)
    {
        returns.push_back(close / previous - 1.0);
        previous = close;
    }
    double mean = 0, squares = 0;
    for (double r : returns)
        mean += r;
    mean /= returns.size();
    for (double r : returns)
        squares += (r - mean) * (r - mean);
    double deviation = std::sqrt(squares / (returns.size() - 1));
    return deviation > 0 ? mean / deviation * std::sqrt(static_cast<double>(MarketTime::TRADING_DAYS_PER_YEAR)) : 0;
}
