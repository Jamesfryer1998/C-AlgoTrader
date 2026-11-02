#include "PairsTrading.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace quant
{
    static Vector logs(const Vector& prices)
    {
        Vector out(prices.size());
        for (size_t i = 0; i < prices.size(); i++)
            out[i] = std::log(prices[i]);
        return out;
    }

    std::vector<std::pair<std::string, CointegrationResult>>
    PairsTrading::testByYear(const std::vector<std::string>& dates, const Vector& y, const Vector& x)
    {
        std::map<std::string, std::pair<Vector, Vector>> byYear;
        for (size_t i = 0; i < dates.size(); i++)
        {
            auto& [ys, xs] = byYear[dates[i].substr(0, 4)];
            ys.push_back(std::log(y[i]));
            xs.push_back(std::log(x[i]));
        }

        std::vector<std::pair<std::string, CointegrationResult>> results;
        for (const auto& [year, series] : byYear)
            if (series.first.size() >= 200)   // skip part years at the edges
                results.emplace_back(year, engleGranger(series.second, series.first));
        return results;
    }

    std::vector<PairScan>
    PairsTrading::scan(const PriceTable& prices)
    {
        std::vector<PairScan> rows;
        for (size_t i = 0; i < prices.symbols.size(); i++)
            for (size_t j = i + 1; j < prices.symbols.size(); j++)
            {
                PairScan row;
                row.y = prices.symbols[i];
                row.x = prices.symbols[j];
                row.test = engleGranger(logs(prices[j]), logs(prices[i]));
                row.returnCorrelation = correlation(logReturns(prices[i]), logReturns(prices[j]));
                rows.push_back(row);
            }
        std::sort(rows.begin(), rows.end(), [](const PairScan& a, const PairScan& b) { return a.test.adf < b.test.adf; });
        return rows;
    }

    PairsResult
    PairsTrading::backtest(const std::string& yName, const std::string& xName,
                           const std::vector<std::string>& dates, const Vector& y, const Vector& x,
                           const PairsSettings& settings)
    {
        size_t n = dates.size();
        size_t lookback = static_cast<size_t>(settings.lookbackDays);
        size_t delay = static_cast<size_t>(std::max(0, settings.signalDelayDays));
        if (y.size() != n || x.size() != n)
            throw std::runtime_error("Pair prices must cover the same dates");
        if (lookback < 20 || n < lookback + delay + 2)
            throw std::runtime_error("Not enough data: need more days than the lookback (" +
                                     std::to_string(lookback) + ") plus a few to trade");

        PairsResult result;
        result.y = yName;
        result.x = xName;

        Vector logY = logs(y), logX = logs(x);
        result.fullSample = engleGranger(logX, logY);
        result.byYear = testByYear(dates, y, x);
        result.returnCorrelation = correlation(logReturns(y), logReturns(x));

        // 1. Signals, decided at each close from the lookback window ending that day
        size_t first = lookback - 1;
        Vector target(n, 0.0), targetHedge(n, 0.0), zAt(n, 0.0), hedgeAt(n, 0.0);
        std::vector<std::string> reasonAt(n);

        int side = 0;
        double frozenAlpha = 0, frozenBeta = 0, frozenSd = 1;
        for (size_t t = first; t < n; t++)
        {
            Vector windowX(logX.begin() + (t + 1 - lookback), logX.begin() + t + 1);
            Vector windowY(logY.begin() + (t + 1 - lookback), logY.begin() + t + 1);
            Regression fit = ols(windowX, windowY);
            double sd = stdev(fit.residuals);
            hedgeAt[t] = fit.beta;

            if (side == 0)
            {
                double z = sd > 0 ? fit.residuals.back() / sd : 0.0;
                zAt[t] = z;
                // Enter when stretched past the entry level, but not if it's already past the stop level
                // (that's a relationship breaking, not an opportunity)
                bool beyondStop = settings.stopZ > 0 && std::fabs(z) >= settings.stopZ;
                if ((z > settings.entryZ || z < -settings.entryZ) && !beyondStop)
                {
                    side = z > 0 ? -1 : +1;   // stretched up: short the spread; stretched down: long it
                    frozenAlpha = fit.alpha;
                    frozenBeta = fit.beta;
                    frozenSd = sd > 0 ? sd : 1.0;
                }
            }
            else
            {
                // While in a trade, measure the spread we actually hold (the hedge ratio fixed at entry)
                double z = (logY[t] - frozenBeta * logX[t] - frozenAlpha) / frozenSd;
                zAt[t] = z;
                double stretch = -side * z;   // how far the spread still is on the side we entered
                if (settings.stopZ > 0 && stretch >= settings.stopZ)
                {
                    side = 0;
                    reasonAt[t] = "stop";
                }
                else if (stretch <= settings.exitZ)
                {
                    side = 0;
                    reasonAt[t] = "reverted";
                }
            }
            target[t] = side;
            targetHedge[t] = side != 0 ? frozenBeta : 0.0;
        }

        // 2. Positions: what was decided `delay` closes earlier. Daily P&L on $1 of gross exposure.
        double cost = settings.costBps / 10000.0;
        double previous = 0;
        PairTrade open;
        double tradeEquity = 1.0;
        int inMarket = 0;

        for (size_t t = first + delay; t < n; t++)
        {
            if (t == 0)
                continue;
            size_t signalDay = t - delay;
            double pos = target[signalDay];
            double hedge = targetHedge[signalDay];

            double returnY = y[t] / y[t - 1] - 1.0;
            double returnX = x[t] / x[t - 1] - 1.0;
            double market = pos != 0 ? pos * (returnY - hedge * returnX) / (1.0 + std::fabs(hedge)) : 0.0;

            double exitCost = 0, entryCost = 0;
            if (pos != previous)
            {
                exitCost = std::fabs(previous) * cost;
                entryCost = std::fabs(pos) * cost;
            }
            double daily = market - exitCost - entryCost;

            // Book-keeping for the trade list. A change at day t happened at the close of day t-1.
            if (pos != previous && previous != 0)
            {
                tradeEquity *= 1.0 - exitCost;
                open.exitDate = dates[t - 1];
                open.exitZ = zAt[signalDay];
                open.exitReason = reasonAt[signalDay].empty() ? "reverted" : reasonAt[signalDay];
                open.returnPct = tradeEquity - 1.0;
                result.trades.push_back(open);
            }
            if (pos != previous && pos != 0)
            {
                open = PairTrade();
                open.entryDate = dates[t - 1];
                open.side = static_cast<int>(pos);
                open.hedgeRatio = hedge;
                open.entryZ = zAt[signalDay];
                tradeEquity = 1.0 - entryCost;
            }
            if (pos != 0)
            {
                tradeEquity *= 1.0 + market;
                open.days++;
                inMarket++;
            }

            result.dates.push_back(dates[t]);
            result.z.push_back(zAt[t]);
            result.hedgeRatio.push_back(hedgeAt[t]);
            result.position.push_back(pos);
            result.dailyReturns.push_back(daily);
            previous = pos;
        }

        if (previous != 0)
        {
            open.exitDate = "open";
            open.exitZ = zAt[n - 1];
            open.exitReason = "end of data";
            open.returnPct = tradeEquity - 1.0;
            result.trades.push_back(open);
        }

        result.performance = performance(result.dailyReturns);
        result.timeInMarket = result.dailyReturns.empty() ? 0.0
                            : static_cast<double>(inMarket) / result.dailyReturns.size();
        return result;
    }
}
