#include "IntradayDay.hpp"

#include <algorithm>
#include "../util/MarketTime.hpp"

const int IntradayDay::LAST_MINUTE = MarketTime::MINUTES_PER_SESSION - 1;

IntradayDay::IntradayDay(const AlpacaMultiBars::BarsBySymbol& bars, const std::vector<size_t>& rows,
                         const std::vector<std::string>& symbols)
{
    for (size_t i = 0; i < rows.size() && i < symbols.size(); i++)
    {
        auto found = bars.find(symbols[i]);
        if (found == bars.end() || found->second.empty() || index.count(rows[i]))
            continue;
        std::vector<double> open(MarketTime::MINUTES_PER_SESSION, 0), last(MarketTime::MINUTES_PER_SESSION, 0);
        for (const auto& bar : found->second)
        {
            int minute = MarketTime::minuteOfDay(bar.time) - MarketTime::OPEN_MINUTE;
            if (minute >= 0 && minute < MarketTime::MINUTES_PER_SESSION && bar.close > 0)
            {
                open[minute] = bar.open;
                last[minute] = bar.close;
            }
        }
        for (int minute = 1; minute < MarketTime::MINUTES_PER_SESSION; minute++)
            if (last[minute] <= 0)
                last[minute] = last[minute - 1];
        index[rows[i]] = opens.size();
        opens.push_back(std::move(open));
        lasts.push_back(std::move(last));
    }
}

double
IntradayDay::price(size_t row, int minute) const
{
    auto found = index.find(row);
    if (found == index.end() || minute < 0)
        return 0;
    return lasts[found->second][std::min(minute, LAST_MINUTE)];
}

double
IntradayDay::fill(size_t row, int minute) const
{
    auto found = index.find(row);
    if (found == index.end() || minute < 0)
        return 0;
    if (minute > LAST_MINUTE)
        return price(row, LAST_MINUTE);
    double open = opens[found->second][minute];
    return open > 0 ? open : price(row, minute - 1);
}
