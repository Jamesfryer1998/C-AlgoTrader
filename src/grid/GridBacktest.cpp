#include "GridBacktest.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include "../morning/StockSelection.hpp"

namespace
{
    const double PERCENT = 100.0;
    const int BUY = 1, SELL = -1, NONE = 0;
}

GridBacktest::GridBacktest(const GridSettings& settings, const PriceMatrix& prices)
    : settings(settings), prices(prices)
{
}

std::vector<size_t>
GridBacktest::fundRows() const
{
    std::vector<size_t> rows;
    for (const auto& symbol : settings.funds)
        if (prices.row(symbol) < prices.stocks())
            rows.push_back(prices.row(symbol));
    return rows;
}

std::vector<size_t>
GridBacktest::mostTraded(size_t day, size_t count) const
{
    std::set<size_t> excluded;
    for (const auto& symbol : settings.funds)
        excluded.insert(prices.row(symbol));
    for (const auto& symbol : settings.notStocks)
        excluded.insert(prices.row(symbol));
    size_t days = static_cast<size_t>(settings.formationDays);
    if (day < days)
        return {};
    std::vector<std::pair<double, size_t>> ranked;
    for (size_t stock = 0; stock < prices.stocks(); stock++)
    {
        if (excluded.count(stock) || prices.close[stock][day - 1] <= settings.minPrice)
            continue;
        double dollars = 0;
        bool everyDay = true;
        for (size_t d = day - days; d < day; d++)
        {
            everyDay = everyDay && prices.close[stock][d] > 0;
            dollars += prices.dollarVolume[stock][d];
        }
        if (everyDay)
            ranked.push_back({-dollars, stock});
    }
    std::sort(ranked.begin(), ranked.end());
    std::vector<size_t> rows;
    for (size_t i = 0; i < ranked.size() && i < count; i++)
        rows.push_back(ranked[i].second);
    return rows;
}

MorningBacktest::Line
GridBacktest::run(const GridSettings::Variant& variant, const std::vector<size_t>& rows, size_t first, size_t last,
                  Mode mode, const std::string& selection) const
{
    MorningBacktest::Line line;
    line.variant = variant.name;
    line.selection = selection;
    line.equity = settings.startingCapital;
    if (rows.empty() || first == 0 || first > last)
        return line;
    std::mt19937_64 random(StockSelection::stableHash(variant.name) ^ settings.randomSeed);
    std::bernoulli_distribution heads(0.5);
    double slippage = settings.slippagePercent / PERCENT;
    double trigger = variant.triggerPercent / PERCENT, step = variant.stepPercent / PERCENT;

    // The starting sleeves, at the close before the first day
    std::vector<Sleeve> sleeves;
    double sleeveValue = settings.startingCapital / rows.size();
    for (size_t row : rows)
    {
        Sleeve sleeve;
        sleeve.row = row;
        sleeve.last = prices.close[row][first - 1];
        sleeve.shares = sleeve.last > 0 ? sleeveValue * settings.startInvested / sleeve.last : 0;
        sleeve.cash = sleeveValue - sleeve.shares * sleeve.last;
        sleeves.push_back(sleeve);
    }
    auto worth = [&]() {
        double total = 0;
        for (const auto& sleeve : sleeves)
            total += sleeve.cash + sleeve.shares * sleeve.last;
        return total;
    };
    // Buy (+) or sell (-) shares at `price`, as far as the sleeve's cash and shares go; adds its costs and counts it
    double costs = 0;
    int fills = 0;
    auto fill = [&](Sleeve& sleeve, double shares, double price) {
        if (price <= 0)
            return;
        shares = shares > 0 ? std::min(shares, sleeve.cash / (price * (1 + slippage))) : -std::min(-shares, sleeve.shares);
        if (shares == 0)
            return;
        double cost = std::fabs(shares) * price * slippage;
        sleeve.shares += shares;
        sleeve.cash -= shares * price + cost;
        costs += cost;
        fills++;
    };

    for (size_t day = first; day <= last && day < prices.days.size(); day++)
    {
        double morning = worth();
        costs = 0;
        fills = 0;
        for (auto& sleeve : sleeves)
        {
            double open = prices.open[sleeve.row][day], close = prices.close[sleeve.row][day];
            if (sleeve.pending != 0 && open > 0)
            {
                fill(sleeve, sleeve.pending, open);
                sleeve.pending = 0;
            }
            if (close <= 0)
                continue;   // no bar: kept at its last price
            double before = sleeve.last;
            sleeve.last = close;
            if (mode == Mode::Held || before <= 0)
                continue;
            double move = close / before - 1;
            int side = move <= -trigger ? BUY : move >= trigger ? SELL : NONE;
            if (side == NONE)
                continue;
            if (variant.mirror)
                side = -side;
            if (mode == Mode::Random)
                side = heads(random) ? BUY : SELL;
            double shares = side * step * sleeve.shares;
            if (variant.atClose)
            {
                fill(sleeve, shares, close);
            }
            else if (day < last)
                sleeve.pending = shares;
        }
        double evening = worth();
        line.days.push_back(prices.days[day]);
        line.returns.push_back(morning > 0 ? evening / morning - 1 : 0);
        line.equity = evening;
        StockDay summary;
        summary.symbol = "sleeves";
        summary.day = prices.days[day];
        summary.trades = fills;
        summary.costs = costs;
        summary.netPnl = evening - morning;
        summary.grossPnl = summary.netPnl + costs;
        summary.wins = fills && summary.netPnl > 0 ? 1 : 0;
        if (fills || mode != Mode::Held)
            line.stockDays.push_back(summary);
    }
    return line;
}
