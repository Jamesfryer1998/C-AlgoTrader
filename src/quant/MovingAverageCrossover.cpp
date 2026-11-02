#include "MovingAverageCrossover.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace quant
{
    Vector
    MovingAverageCrossover::simpleMovingAverage(const Vector& closes, int days)
    {
        Vector out(closes.size(), std::numeric_limits<double>::quiet_NaN());
        if (days <= 0)
            return out;
        double sum = 0;
        for (size_t i = 0; i < closes.size(); i++)
        {
            sum += closes[i];
            if (i >= static_cast<size_t>(days))
                sum -= closes[i - days];
            if (i + 1 >= static_cast<size_t>(days))
                out[i] = sum / days;
        }
        return out;
    }

    namespace
    {
        struct Run
        {
            Vector position;
            Vector returns;
            int trades = 0;
            double costs = 0;
        };

        // Daily returns of the strategy over days first..n-1, where the position held during day t
        // is the signal from `delay` closes earlier
        Run simulate(const std::vector<bool>& signal, const Vector& closes, size_t signalStart, size_t first,
                     size_t delay, double cost)
        {
            Run run;
            double previous = 0;
            for (size_t t = first; t < closes.size(); t++)
            {
                double pos = (t >= signalStart + delay && signal[t - delay]) ? 1.0 : 0.0;
                double market = pos * (closes[t] / closes[t - 1] - 1.0);
                double traded = std::fabs(pos - previous) * cost;
                if (pos > previous)
                    run.trades++;
                run.costs += traded;
                run.position.push_back(pos);
                run.returns.push_back(market - traded);
                previous = pos;
            }
            return run;
        }
    }

    CrossoverResult
    MovingAverageCrossover::backtest(const std::vector<std::string>& dates, const Vector& closes,
                                     const CrossoverSettings& settings)
    {
        if (settings.fastDays <= 0 || settings.slowDays <= settings.fastDays)
            throw std::runtime_error("Need 0 < fast days < slow days");
        size_t n = closes.size();
        size_t signalStart = static_cast<size_t>(settings.slowDays) - 1;   // first day both averages exist
        size_t first = signalStart + 1;                                     // first day with a return to earn
        if (dates.size() != n || n < first + 2)
            throw std::runtime_error("Not enough data: need more than " + std::to_string(settings.slowDays) + " days");

        CrossoverResult result;
        Vector fast = simpleMovingAverage(closes, settings.fastDays);
        Vector slow = simpleMovingAverage(closes, settings.slowDays);

        // Invested while the fast average is above the slow one (decided at each close)
        std::vector<bool> signal(n, false);
        for (size_t t = signalStart; t < n; t++)
            signal[t] = fast[t] > slow[t];

        double cost = settings.costBps / 10000.0;
        size_t delay = static_cast<size_t>(std::max(0, settings.signalDelayDays));
        Run honest = simulate(signal, closes, signalStart, first, delay, cost);
        Run noCosts = simulate(signal, closes, signalStart, first, delay, 0.0);
        Run peeking = simulate(signal, closes, signalStart, first, 0, cost);

        // Buy and hold over exactly the same days, paying for one buy on the first day
        for (size_t t = first; t < n; t++)
        {
            double r = closes[t] / closes[t - 1] - 1.0;
            result.buyHoldReturns.push_back(t == first ? r - cost : r);
        }

        for (size_t t = first; t < n; t++)
        {
            result.dates.push_back(dates[t]);
            result.closes.push_back(closes[t]);
            result.fastAverage.push_back(fast[t]);
            result.slowAverage.push_back(slow[t]);
        }
        result.position = honest.position;
        result.strategyReturns = honest.returns;
        result.trades = honest.trades;
        result.costsPaid = honest.costs;

        double invested = 0;
        for (double p : honest.position)
            invested += p;
        result.timeInMarket = honest.position.empty() ? 0.0 : invested / honest.position.size();

        result.strategy = performance(honest.returns);
        result.strategyNoCosts = performance(noCosts.returns);
        result.lookAhead = performance(peeking.returns);
        result.buyHold = performance(result.buyHoldReturns);
        return result;
    }
}
