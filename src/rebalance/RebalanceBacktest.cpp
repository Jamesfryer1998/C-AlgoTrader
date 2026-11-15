#include "RebalanceBacktest.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    const size_t YEAR_MONTH = 7;   // "2026-03" of "2026-03-02"
    const double PERCENT = 100.0;
    const double ALL_IN = 1.0;
    const int SPREAD_LEGS = 2, ONE_LEG = 1;

    double change(const std::vector<double>& closes, size_t day)
    {
        return day > 0 && closes[day] > 0 && closes[day - 1] > 0 ? closes[day] / closes[day - 1] - 1 : 0;
    }
}

RebalanceBacktest::RebalanceBacktest(const RebalanceSettings& settings, const std::vector<std::string>& days,
                                     const std::vector<double>& stocks,
                                     const std::map<std::string, std::vector<double>>& bonds)
    : settings(settings), days(days), stocks(stocks), bonds(bonds)
{
}

bool
RebalanceBacktest::sameMonth(size_t a, size_t b) const
{
    return days[a].substr(0, YEAR_MONTH) == days[b].substr(0, YEAR_MONTH);
}

size_t
RebalanceBacktest::firstOfMonth(size_t day) const
{
    while (day > 0 && sameMonth(day - 1, day))
        day--;
    return day;
}

int
RebalanceBacktest::daysToMonthEnd(size_t day) const
{
    int count = 1;
    while (day + count < days.size() && sameMonth(day + count, day))
        count++;
    return count;
}

std::vector<double>
RebalanceBacktest::drift(const std::string& bond) const
{
    std::vector<double> result(days.size(), 0);
    auto found = bonds.find(bond);
    if (found == bonds.end())
        return result;
    const auto& bondCloses = found->second;
    double target = settings.targetStockWeight;
    for (size_t day = 0; day < days.size(); day++)
    {
        size_t start = firstOfMonth(day);
        if (start == 0)
            continue;   // no month end before it
        size_t monthEnd = start - 1;
        if (stocks[monthEnd] <= 0 || bondCloses[monthEnd] <= 0 || stocks[day] <= 0 || bondCloses[day] <= 0)
            continue;
        double stockValue = target * stocks[day] / stocks[monthEnd];
        double bondValue = (1 - target) * bondCloses[day] / bondCloses[monthEnd];
        result[day] = stockValue / (stockValue + bondValue) - target;
    }
    return result;
}

double
RebalanceBacktest::sizeOf(const RebalanceSettings::Variant& variant, double drifted) const
{
    if (variant.sizing == RebalanceSettings::Sizing::Sign)
        return drifted > 0 ? ALL_IN : drifted < 0 ? -ALL_IN : 0;
    return settings.fullAtDrift > 0 ? std::clamp(drifted / settings.fullAtDrift, -ALL_IN, ALL_IN) : 0;
}

std::vector<double>
RebalanceBacktest::positions(const RebalanceSettings::Variant& variant) const
{
    std::vector<double> drifts = drift(variant.bond);
    std::vector<double> held(days.size(), 0);
    size_t window = static_cast<size_t>(std::max(settings.windowDays, 0));
    for (size_t day = 1; day < days.size(); day++)
    {
        // A month's first day is never in its last days (except where the data stops, mid-month)
        if (firstOfMonth(day) == day)
            held[day] = settings.reversalDay && day > window + 1 ? sizeOf(variant, drifts[day - window - 1]) : 0;   // the unwinding
        else if (daysToMonthEnd(day) <= settings.windowDays)
            held[day] = -sizeOf(variant, drifts[day - 1]);   // ahead of the funds' trade
    }
    return held;
}

std::vector<double>
RebalanceBacktest::coinFlipped(const std::vector<double>& positions, std::mt19937_64& random) const
{
    std::bernoulli_distribution heads(0.5);
    std::vector<double> flipped = positions;
    std::string month;
    double coin = ALL_IN;
    for (size_t day = 1; day < days.size(); day++)
    {
        // A reversal day belongs with the window before it
        std::string owner = firstOfMonth(day) == day ? days[day - 1].substr(0, YEAR_MONTH) : days[day].substr(0, YEAR_MONTH);
        if (owner != month)
        {
            month = owner;
            coin = heads(random) ? ALL_IN : -ALL_IN;
        }
        flipped[day] = positions[day] * coin;
    }
    return flipped;
}

RebalanceBacktest::Traded
RebalanceBacktest::trade(const RebalanceSettings::Variant& variant, const std::vector<double>& positions, size_t first,
                         size_t last) const
{
    Traded traded;
    const auto& bondCloses = bonds.at(variant.bond);
    bool spread = variant.legs == RebalanceSettings::Legs::Spread;
    double slippage = variant.slippagePercent / PERCENT * (spread ? SPREAD_LEGS : ONE_LEG);
    double before = 0;
    for (size_t day = std::max<size_t>(first, 1); day <= last && day < days.size(); day++)
    {
        double held = variant.legs == RebalanceSettings::Legs::StocksLongOnly ? std::max(positions[day], 0.0) : positions[day];
        double move = spread ? change(stocks, day) - change(bondCloses, day) : change(stocks, day);
        double cost = std::fabs(held - before) * slippage;
        traded.days.push_back(days[day]);
        traded.returns.push_back(held * move - cost);
        traded.changes.push_back(held != before ? 1 : 0);
        traded.costs.push_back(cost);
        before = held;
    }
    return traded;
}

MorningBacktest::Line
RebalanceBacktest::line(const Traded& traded, const std::string& variant, const std::string& selection,
                        const std::string& symbol) const
{
    MorningBacktest::Line line;
    line.variant = variant;
    line.selection = selection;
    line.equity = settings.startingCapital;
    for (size_t i = 0; i < traded.days.size(); i++)
    {
        double morning = line.equity;
        line.days.push_back(traded.days[i]);
        line.returns.push_back(traded.returns[i]);
        line.equity *= 1 + traded.returns[i];
        if (!traded.changes[i] && traded.returns[i] == 0)
            continue;
        StockDay day;
        day.symbol = symbol;
        day.day = traded.days[i];
        day.trades = traded.changes[i];
        day.costs = traded.costs[i] * morning;
        day.netPnl = line.equity - morning;
        day.grossPnl = day.netPnl + day.costs;
        day.wins = day.trades && day.netPnl > 0 ? 1 : 0;
        line.stockDays.push_back(day);
    }
    return line;
}
