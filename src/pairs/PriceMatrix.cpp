#include "PriceMatrix.hpp"

#include <algorithm>
#include <utility>

namespace
{
    const size_t YEAR_MONTH = 7;   // "2026-03" of "2026-03-02"
}

void
PriceMatrix::add(const std::string& symbol, const std::vector<quant::DailyBar>& bars)
{
    std::map<std::string, const quant::DailyBar*> byDate;
    for (const auto& bar : bars)
        byDate[bar.date] = &bar;
    std::vector<double> closes(days.size(), 0), opens(days.size(), 0), dollars(days.size(), 0);
    for (size_t day = 0; day < days.size(); day++)
    {
        auto found = byDate.find(days[day]);
        if (found == byDate.end())
            continue;
        closes[day] = found->second->close;
        opens[day] = found->second->open;
        dollars[day] = found->second->close * found->second->volume;
    }
    symbols.push_back(symbol);
    close.push_back(std::move(closes));
    open.push_back(std::move(opens));
    dollarVolume.push_back(std::move(dollars));
}

std::vector<size_t>
PriceMatrix::monthStarts(size_t first) const
{
    std::vector<size_t> starts;
    for (size_t day = std::max<size_t>(first, 1); day < days.size(); day++)
        if (days[day].substr(0, YEAR_MONTH) != days[day - 1].substr(0, YEAR_MONTH))
            starts.push_back(day);
    return starts;
}

size_t
PriceMatrix::indexOnOrAfter(const std::string& date) const
{
    return static_cast<size_t>(std::lower_bound(days.begin(), days.end(), date) - days.begin());
}

std::vector<size_t>
PriceMatrix::liquid(size_t formationEnd, int lookback, double minPrice, double minDollarVolume, size_t count) const
{
    if (lookback <= 0 || formationEnd < static_cast<size_t>(lookback) || formationEnd > days.size())
        return {};
    std::vector<std::pair<double, size_t>> ranked;
    for (size_t stock = 0; stock < stocks(); stock++)
    {
        if (close[stock][formationEnd - 1] <= minPrice)
            continue;
        double total = 0;
        bool everyDay = true;
        for (size_t day = formationEnd - lookback; day < formationEnd && everyDay; day++)
        {
            everyDay = close[stock][day] > 0;
            total += dollarVolume[stock][day];
        }
        double average = total / lookback;
        if (everyDay && average >= minDollarVolume)
            ranked.push_back({average, stock});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    std::vector<size_t> universe;
    for (size_t i = 0; i < ranked.size() && i < count; i++)
        universe.push_back(ranked[i].second);
    return universe;
}

double
PriceMatrix::dailyReturn(size_t stock, size_t day) const
{
    if (day == 0 || close[stock][day] <= 0 || close[stock][day - 1] <= 0)
        return 0;
    return close[stock][day] / close[stock][day - 1] - 1;
}
