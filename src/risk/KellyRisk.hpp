#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "RiskEngine.hpp"
#include "../util/Format.hpp"

/**
 * KELLY - the Kelly criterion (John L. Kelly Jr., 1956): the share of the account that grows it fastest,
 * worked out from how this stock's own trades have gone
 *
 *   "risk": {"name": "KELLY", "kelly_fraction": 0.5, "lookback_trades": 50, "min_trades": 20, "starting_percent": 5}
 *
 * From the stock's last lookback_trades closed trades (returns as a share of the position):
 *
 *   p = share of trades that won, q = share that lost
 *   W = average winning return, L = average losing return (as a positive number)
 *
 *   Kelly position = p / L - q / W          of equity, the f that maximises p ln(1 + fW) + q ln(1 - fL)
 *   shares         = equity x Kelly position x kelly_fraction / price
 *
 * e.g. 55% winners, +1.2% average win, -1.0% average loss: 0.55 / 0.010 - 0.45 / 0.012 = 17.5, i.e. 1750% of
 * equity. Intraday trades move the position by around 1%, so even a small edge asks for leverage; the
 * max_stock_percent and max_portfolio_percent caps are what really limit it. A stock whose trades have no
 * edge gets 0: Kelly says don't trade it.
 *
 * Full Kelly is very jumpy and the edge is only an estimate, so kelly_fraction scales it down (0.5 = half
 * Kelly, the usual choice). Until a stock has min_trades closed trades there's nothing to estimate from,
 * so each position is starting_percent of equity (as FIXED_FRACTION).
 */
class KellyRisk : public RiskEngine
{
    public:
        static constexpr const char* NAME = "KELLY";
        static constexpr double DEFAULT_KELLY_FRACTION = 0.5;   // half Kelly
        static constexpr int DEFAULT_LOOKBACK_TRADES = 50;
        static constexpr int DEFAULT_MIN_TRADES = 20;
        static constexpr double DEFAULT_STARTING_PERCENT = 5.0;

        explicit KellyRisk(const json& settings)
            : RiskEngine(settings),
              kellyFraction(settings.value("kelly_fraction", DEFAULT_KELLY_FRACTION)),
              lookbackTrades(settings.value("lookback_trades", DEFAULT_LOOKBACK_TRADES)),
              minTrades(settings.value("min_trades", DEFAULT_MIN_TRADES)),
              startingPercent(settings.value("starting_percent", DEFAULT_STARTING_PERCENT)) {}

        std::string describe() const override
        {
            return std::string(NAME) + " x" + Format::number(kellyFraction, 2) + " (last " + std::to_string(lookbackTrades)
                 + " trades, " + Format::number(startingPercent, 1) + "% until " + std::to_string(minTrades) + ")";
        }

        // Full Kelly as a share of equity (1 = 100%) from closed-trade returns (0.01 = +1%). 0 if there's no
        // edge; unlimited if no trade has lost yet (the caps then decide).
        static double kellyPosition(const std::vector<double>& returns)
        {
            double wins = 0, losses = 0, winTotal = 0, lossTotal = 0;
            for (double r : returns)
            {
                if (r > 0) { wins++; winTotal += r; }
                if (r < 0) { losses++; lossTotal -= r; }
            }
            if (returns.empty() || wins == 0)
                return 0;
            if (losses == 0)
                return std::numeric_limits<double>::infinity();

            double n = static_cast<double>(returns.size());
            double p = wins / n, q = losses / n;
            double averageWin = winTotal / wins, averageLoss = lossTotal / losses;
            return std::max(0.0, p / averageLoss - q / averageWin);
        }

    protected:
        double targetShares(const Order& order, const SizingContext& context) const override
        {
            static const std::vector<double> NO_TRADES;
            const std::vector<double>& history = context.closedTradeReturns ? *context.closedTradeReturns : NO_TRADES;
            if (static_cast<int>(history.size()) < minTrades)
                return percentOf(context.equity, startingPercent) / order.getPrice();

            size_t count = std::min(history.size(), static_cast<size_t>(std::max(1, lookbackTrades)));
            std::vector<double> recent(history.end() - static_cast<long>(count), history.end());
            double position = kellyPosition(recent) * kellyFraction;
            if (std::isinf(position))
                return std::numeric_limits<double>::max();   // capped by max_stock_percent / max_portfolio_percent
            return context.equity * position / order.getPrice();
        }

    private:
        double kellyFraction;
        int lookbackTrades;
        int minTrades;
        double startingPercent;
};
