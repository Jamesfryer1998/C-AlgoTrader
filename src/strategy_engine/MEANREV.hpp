#pragma once

#include "StrategyBase.hpp"
#include "../util/Log.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

/**
 * MEANREV - mean reversion
 *
 * How far is the price from its recent average, in standard deviations?
 *   z = (close - average of the last moving_average_period closes) / std dev of the last lookback_period closes
 *
 *   No position and z <= -entry_threshold -> BUY  (unusually cheap, expect it to come back up)
 *   No position and z >= +entry_threshold -> SELL (unusually expensive, expect it to come back down)
 *   Long and z has come back to >= -exit_threshold  -> SELL to close
 *   Short and z has come back to <= +exit_threshold -> BUY to close
 *
 * The strategy remembers the position it asked for (long / short / flat). If the OMS blocks one of
 * its orders the two can differ; the stop loss / take profit on each entry still protects it.
 */
class MEANREV : public StrategyBase {
    public:
        MEANREV(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) {}

        void execute() override
        {
            int needed = std::max(_strategyAttribute.lookback_period, _strategyAttribute.moving_average_period);
            std::vector<float> closes = recentCloses(needed);
            if (closes.empty())
                return;   // not enough bars yet

            double z = zScore(closes, _strategyAttribute.moving_average_period, _strategyAttribute.lookback_period,
                              &lastMean, &lastSd);
            lastClose = closes.back();
            lastZ = z;
            ready = true;
            double entry = _strategyAttribute.entry_threshold;
            double exit = _strategyAttribute.exit_threshold;

            if (state == 0 && z <= -entry)      { signal(OrderType::BUY);  state = +1; }
            else if (state == 0 && z >= entry)  { signal(OrderType::SELL); state = -1; }
            else if (state > 0 && z >= -exit)   { signal(OrderType::SELL); state = 0; }
            else if (state < 0 && z <= exit)    { signal(OrderType::BUY);  state = 0; }
            else
                return;

            Log::out() << "MEANREV " << order.getTypeAsString() << " | z-score " << std::fixed << std::setprecision(2) << z
                       << " (entry " << entry << ", exit " << exit << ")" << std::endl;
        }

        // z-score of the newest close. closes must hold at least max(maPeriod, sdPeriod) values.
        static double zScore(const std::vector<float>& closes, int maPeriod, int sdPeriod,
                             double* meanOut = nullptr, double* sdOut = nullptr)
        {
            size_t n = closes.size();
            double mean = 0;
            for (size_t i = n - maPeriod; i < n; i++)
                mean += closes[i];
            mean /= maPeriod;

            double sdMean = 0;
            for (size_t i = n - sdPeriod; i < n; i++)
                sdMean += closes[i];
            sdMean /= sdPeriod;
            double variance = 0;
            for (size_t i = n - sdPeriod; i < n; i++)
                variance += (closes[i] - sdMean) * (closes[i] - sdMean);
            double sd = std::sqrt(variance / sdPeriod);
            if (meanOut) *meanOut = mean;
            if (sdOut) *sdOut = sd;

            return sd > 0 ? (closes.back() - mean) / sd : 0.0;
        }

        void validate() override
        {
            if(_strategyAttribute.lookback_period < 2)
                throw std::runtime_error("MEANREV lookback_period must be at least 2");
            if(_strategyAttribute.moving_average_period < 1)
                throw std::runtime_error("MEANREV moving_average_period not set");
            if(_strategyAttribute.entry_threshold <= 0)
                throw std::runtime_error("MEANREV entry_threshold must be above 0");
            if(_strategyAttribute.exit_threshold < 0 || _strategyAttribute.exit_threshold >= _strategyAttribute.entry_threshold)
                throw std::runtime_error("MEANREV exit_threshold must be between 0 and entry_threshold");
        }

        int getState() const { return state; }

        // Price levels that would trigger the next trade:
        //   flat:  "MEANREV(20/120)   L: 205.10 | C: 206.52 | U: 209.80   (z -0.42, BUY below L, SELL above U)"
        //   long:  "MEANREV(20/120)   long, C: 204.00 | exit at >= 207.45   (z -1.80)"
        //   short: "MEANREV(20/120)   short, C: 211.00 | exit at <= 207.45   (z +2.10)"
        std::string status() const override
        {
            if (!ready)
                return "";
            double entry = _strategyAttribute.entry_threshold;
            double exit = _strategyAttribute.exit_threshold;
            std::ostringstream out;
            out << std::fixed << std::setprecision(2) << "MEANREV(" << _strategyAttribute.moving_average_period
                << "/" << _strategyAttribute.lookback_period << ")   ";
            if (state == 0)
                out << "L: " << lastMean - entry * lastSd << " | C: " << lastClose << " | U: " << lastMean + entry * lastSd
                    << "   (z " << (lastZ >= 0 ? "+" : "") << lastZ << ", BUY below L, SELL above U)";
            else if (state > 0)
                out << "long, C: " << lastClose << " | exit at >= " << lastMean - exit * lastSd
                    << "   (z " << (lastZ >= 0 ? "+" : "") << lastZ << ")";
            else
                out << "short, C: " << lastClose << " | exit at <= " << lastMean + exit * lastSd
                    << "   (z " << (lastZ >= 0 ? "+" : "") << lastZ << ")";
            return out.str();
        }

    private:
        int state = 0;   // +1 long, -1 short, 0 flat (the position this strategy asked for)
        double lastMean = 0, lastSd = 0, lastClose = 0, lastZ = 0;
        bool ready = false;
};
