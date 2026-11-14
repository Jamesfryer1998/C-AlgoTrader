#include "LeadLagBacktest.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <set>
#include "PositionBook.hpp"
#include "../morning/StockSelection.hpp"

namespace
{
    const int SIDES = 2;   // a long side and a short side

    // Each stock's daily returns over the formation year, scaled to mean 0 and size 1 (so a dot product
    // divided by the days is their correlation)
    std::vector<double> standardisedReturns(const PriceMatrix& prices, size_t stock, size_t from, size_t to)
    {
        std::vector<double> returns;
        for (size_t day = from; day < to; day++)
            returns.push_back(prices.dailyReturn(stock, day));
        double mean = 0, squares = 0;
        for (double value : returns)
            mean += value;
        mean /= returns.size();
        for (double value : returns)
            squares += (value - mean) * (value - mean);
        double deviation = std::sqrt(squares / returns.size());
        for (double& value : returns)
            value = deviation > 0 ? (value - mean) / deviation : 0;
        return returns;
    }
}

std::map<size_t, size_t>
LeadLagBacktest::closestLeaders(const std::vector<size_t>& universe, size_t formationEnd) const
{
    size_t from = formationEnd - settings.formationDays + 1;   // returns need the day before
    std::vector<std::vector<double>> returns;
    for (size_t stock : universe)
        returns.push_back(standardisedReturns(prices, stock, from, formationEnd));
    std::map<size_t, size_t> leaders;
    for (size_t follower = 1; follower < universe.size(); follower++)
    {
        double best = settings.minCorrelation;
        long leader = -1;
        for (size_t bigger = 0; bigger < follower; bigger++)
        {
            double together = 0;
            for (size_t day = 0; day < returns[follower].size(); day++)
                together += returns[follower][day] * returns[bigger][day];
            double correlation = together / returns[follower].size();
            if (correlation >= best)
            {
                best = correlation;
                leader = static_cast<long>(bigger);
            }
        }
        if (leader >= 0)
            leaders[universe[follower]] = universe[leader];
    }
    return leaders;
}

std::map<size_t, size_t>
LeadLagBacktest::randomLeaders(const std::vector<size_t>& universe, size_t formationEnd) const
{
    std::mt19937_64 random(StockSelection::stableHash(prices.days[formationEnd]) ^ settings.randomSeed);
    std::map<size_t, size_t> leaders;
    for (size_t follower = 1; follower < universe.size(); follower++)
        leaders[universe[follower]] = universe[std::uniform_int_distribution<size_t>(0, follower - 1)(random)];
    return leaders;
}

void
LeadLagBacktest::run(const PairsSettings::Variant& variant, bool matched, size_t first, size_t last,
                     MorningBacktest::Line& line) const
{
    PositionBook book(prices, costs);
    std::multimap<size_t, long> closeOn;   // day -> trades sold at its close
    std::map<size_t, size_t> leaders;
    std::vector<size_t> starts = prices.monthStarts(first);
    std::set<size_t> monthStart(starts.begin(), starts.end());
    size_t eachSide = settings.stocksEachSide;

    for (size_t day = first; day <= last && day < prices.days.size(); day++)
    {
        double morning = line.equity;
        double pnl = book.mark(day);

        if ((monthStart.count(day) || leaders.empty()) && day >= static_cast<size_t>(settings.formationDays))
        {
            std::vector<size_t> universe = prices.liquid(day, settings.formationDays, settings.minPrice,
                                                         settings.minDollarVolume, settings.universeSize);
            leaders = matched ? closestLeaders(universe, day) : randomLeaders(universe, day);
        }

        // Sell what's due today
        auto [due, end] = closeOn.equal_range(day);
        for (auto trade = due; trade != end; ++trade)
            book.close(trade->second, line, pnl);
        closeOn.erase(day);

        // Today's gaps: how much each leader beat its follower
        std::vector<std::pair<double, size_t>> gaps;
        for (const auto& [follower, leader] : leaders)
        {
            if (prices.close[follower][day] <= 0 || prices.close[leader][day] <= 0)
                continue;
            double gap = prices.dailyReturn(leader, day) - prices.dailyReturn(follower, day);
            if (gap != 0)
                gaps.push_back({gap, follower});
        }
        std::sort(gaps.begin(), gaps.end());
        double legValue = settings.leverage * morning / (variant.holdDays * SIDES * eachSide);
        auto buyOrShort = [&](size_t stock, int side) {
            long trade = book.open({{stock, side}}, legValue, day, pnl);
            if (trade >= 0)
                closeOn.insert({day + variant.holdDays, trade});
        };
        for (size_t i = 0; i < gaps.size() && i < eachSide && gaps[i].first < 0; i++)
            buyOrShort(gaps[i].second, -1);                       // the leader trailed: short the follower
        for (size_t i = 0; i < gaps.size() && i < eachSide && gaps[gaps.size() - 1 - i].first > 0; i++)
            buyOrShort(gaps[gaps.size() - 1 - i].second, 1);      // the leader beat it: buy the follower

        line.days.push_back(prices.days[day]);
        line.returns.push_back(morning > 0 ? pnl / morning : 0);
        line.equity += pnl;
    }
}
