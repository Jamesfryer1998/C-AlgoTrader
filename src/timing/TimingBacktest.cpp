#include "TimingBacktest.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include "../quant/Stats.hpp"

namespace
{
    const double PERCENT = 100.0;
    const double ALL_IN = 1.0, ALL_OUT = 0.0;
    const double TRADING_DAYS = 252;

    double annualised(double growth, int days)
    {
        return days > 0 && growth > 0 ? std::pow(growth, TRADING_DAYS / days) - 1 : 0;
    }
}

TimingBacktest::TimingBacktest(const TimingSettings& settings, const TimingSeries& series)
    : settings(settings), series(series), periods(settings.forReport())
{
}

TimingBacktest::Traded
TimingBacktest::trade(const std::vector<double>& targets, size_t first, size_t last) const
{
    Traded traded;
    traded.firstIndex = std::max<size_t>(first, 1);
    double slippage = settings.slippagePercent / PERCENT;
    int legs = settings.cashSymbol.empty() ? 1 : 2;
    double held = ALL_OUT;   // flat before the first day
    for (size_t day = std::max<size_t>(first, 1); day <= last && day < series.size(); day++)
    {
        double target = targets[day - 1];
        bool whole = target == ALL_IN || target == ALL_OUT;
        double now = (whole ? target != held : std::fabs(target - held) > settings.rebalanceBand) ? target : held;
        double night = series.open[day] / series.close[day - 1] - 1;
        double session = series.close[day] / series.open[day] - 1;
        double invested = (1 + held * night) * (1 + now * session) - 1;
        double cost = std::fabs(now - held) * slippage * legs;
        traded.days.push_back(series.days[day]);
        traded.returns.push_back(invested + (1 - now) * series.cashReturn[day] - cost);
        traded.exposure.push_back(now);
        traded.changes.push_back(now != held ? 1 : 0);
        traded.costs.push_back(cost);
        held = now;
    }
    return traded;
}

std::vector<double>
TimingBacktest::shuffled(const std::vector<double>& targets, size_t first, size_t last, std::mt19937_64& random) const
{
    size_t from = first > 0 ? first - 1 : 0, to = std::min(last, series.size() - 1);
    std::vector<std::vector<double>> stretches;
    for (size_t day = from; day < to; day++)
        if (stretches.empty() || targets[day] != stretches.back().back())
            stretches.push_back({targets[day]});
        else
            stretches.back().push_back(targets[day]);
    std::shuffle(stretches.begin(), stretches.end(), random);
    std::vector<double> result = targets;
    size_t day = from;
    for (const auto& stretch : stretches)
        for (double value : stretch)
            result[day++] = value;
    return result;
}

TimingBacktest::Measures
TimingBacktest::measure(const Traded& traded, const std::string& period) const
{
    Measures measures;
    std::vector<double> returns, own;
    double exposure = 0;
    std::vector<double> cash;
    for (size_t i = 0; i < traded.days.size(); i++)
    {
        if (periods.periodOf(traded.days[i]) != period)
            continue;
        returns.push_back(traded.returns[i]);
        exposure += traded.exposure[i];
        measures.trades += traded.changes[i];
        size_t day = traded.firstIndex + i;
        own.push_back(series.close[day] / series.close[day - 1] - 1);
        cash.push_back(series.cashReturn[day]);
    }
    if (returns.empty())
        return measures;
    quant::Performance performance = quant::performance(returns);
    measures.days = performance.days;
    measures.cagr = performance.cagr;
    measures.sharpe = performance.sharpe;
    measures.maxDrawdown = performance.maxDrawdown;
    measures.totalReturn = performance.totalReturn;
    measures.exposure = exposure / returns.size();
    double mix = 1, held = 1;
    for (size_t i = 0; i < own.size(); i++)
    {
        mix *= 1 + measures.exposure * own[i] + (1 - measures.exposure) * cash[i];
        held *= 1 + own[i];
    }
    measures.mixCagr = annualised(mix, measures.days);
    measures.edge = measures.cagr - measures.mixCagr;
    double ownCagr = annualised(held, measures.days);
    measures.capture = ownCagr != 0 ? measures.cagr / ownCagr : 0;
    return measures;
}

TimingBacktest::Traded
TimingBacktest::buyAndHold(size_t first, size_t last) const
{
    Traded traded;
    traded.firstIndex = std::max<size_t>(first, 1);
    for (size_t day = traded.firstIndex; day <= last && day < series.size(); day++)
    {
        traded.days.push_back(series.days[day]);
        traded.returns.push_back(series.close[day] / series.close[day - 1] - 1);
        traded.exposure.push_back(ALL_IN);
        traded.changes.push_back(0);
        traded.costs.push_back(0);
    }
    return traded;
}

MorningBacktest::Line
TimingBacktest::line(const Traded& traded, const std::string& variant, const std::string& selection) const
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
        StockDay day;
        day.symbol = settings.symbol;
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

double
TimingBacktest::spearman(const std::vector<double>& a, const std::vector<double>& b)
{
    auto ranks = [](const std::vector<double>& values) {
        std::vector<size_t> order(values.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return values[x] < values[y]; });
        std::vector<double> rank(values.size());
        for (size_t i = 0; i < order.size();)   // ties share their average rank
        {
            size_t j = i;
            while (j + 1 < order.size() && values[order[j + 1]] == values[order[i]])
                j++;
            for (size_t k = i; k <= j; k++)
                rank[order[k]] = (i + j) / 2.0;
            i = j + 1;
        }
        return rank;
    };
    if (a.size() != b.size() || a.size() < 2)
        return 0;
    std::vector<double> x = ranks(a), y = ranks(b);
    double meanX = std::accumulate(x.begin(), x.end(), 0.0) / x.size(), meanY = std::accumulate(y.begin(), y.end(), 0.0) / y.size();
    double covariance = 0, varianceX = 0, varianceY = 0;
    for (size_t i = 0; i < x.size(); i++)
    {
        covariance += (x[i] - meanX) * (y[i] - meanY);
        varianceX += (x[i] - meanX) * (x[i] - meanX);
        varianceY += (y[i] - meanY) * (y[i] - meanY);
    }
    return varianceX > 0 && varianceY > 0 ? covariance / std::sqrt(varianceX * varianceY) : 0;
}
