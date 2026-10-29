#pragma once

#include "StrategyBase.hpp"
#include "../util/Log.hpp"
#include <iomanip>

/**
 * MACD - Moving Average Convergence Divergence
 *
 *   MACD line   = EMA(short_period) - EMA(long_period) of closes
 *   Signal line = EMA(signal_period) of the MACD line
 *   Histogram   = MACD - signal (momentum)
 *
 * signal_type "crossover" (default):
 *   MACD crosses above the signal line -> BUY,  crosses below -> SELL
 * signal_type "zero_cross":
 *   MACD crosses above zero -> BUY,  crosses below zero -> SELL
 *
 * Signals are raised only on the bar where the cross happens.
 * EMAs are worked out over the last 5 x (long + signal) bars, which is long enough for the starting
 * value to stop mattering (< 0.1% weight left), so the cost per bar is fixed.
 */
class MACD : public StrategyBase {
    public:
        MACD(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) {}

        struct Values { double macd = 0, signal = 0, previousMacd = 0, previousSignal = 0; };

        void execute() override
        {
            int window = 5 * (_strategyAttribute.long_period + _strategyAttribute.signal_period);
            std::vector<float> closes = recentCloses(window);
            if (closes.empty())
                return;   // not enough bars yet

            Values v = calculate(closes, _strategyAttribute.short_period,
                                 _strategyAttribute.long_period, _strategyAttribute.signal_period);

            bool zeroCross = _strategyAttribute.signal_type == "zero_cross";
            double lineNow  = zeroCross ? 0.0 : v.signal;
            double lineThen = zeroCross ? 0.0 : v.previousSignal;

            if (v.previousMacd <= lineThen && v.macd > lineNow)
                signal(OrderType::BUY);
            else if (v.previousMacd >= lineThen && v.macd < lineNow)
                signal(OrderType::SELL);
            else
                return;

            Log::out() << "MACD " << order.getTypeAsString() << " | MACD " << std::fixed << std::setprecision(4) << v.macd
                       << " signal " << v.signal << " histogram " << (v.macd - v.signal)
                       << " (" << _strategyAttribute.signal_type << ")" << std::endl;
        }

        // MACD and signal line for the newest bar and the one before it
        static Values calculate(const std::vector<float>& closes, int shortPeriod, int longPeriod, int signalPeriod)
        {
            double shortAlpha = 2.0 / (shortPeriod + 1);
            double longAlpha = 2.0 / (longPeriod + 1);
            double signalAlpha = 2.0 / (signalPeriod + 1);

            double shortEma = closes[0], longEma = closes[0];
            double macd = 0, signalLine = 0;
            Values v;
            for (size_t i = 1; i < closes.size(); i++)
            {
                shortEma += shortAlpha * (closes[i] - shortEma);
                longEma += longAlpha * (closes[i] - longEma);
                v.previousMacd = macd;
                v.previousSignal = signalLine;
                macd = shortEma - longEma;
                signalLine += signalAlpha * (macd - signalLine);
            }
            v.macd = macd;
            v.signal = signalLine;
            return v;
        }

        void validate() override
        {
            if(_strategyAttribute.short_period <= 0)
                throw std::runtime_error("MACD short_period not set");
            if(_strategyAttribute.long_period <= 0)
                throw std::runtime_error("MACD long_period not set");
            if(_strategyAttribute.signal_period <= 0)
                throw std::runtime_error("MACD signal_period not set");
            if(_strategyAttribute.short_period >= _strategyAttribute.long_period)
                throw std::runtime_error("MACD short_period must be less than long_period");
            if(_strategyAttribute.signal_type != "crossover" && _strategyAttribute.signal_type != "zero_cross")
                throw std::runtime_error("MACD signal_type must be \"crossover\" or \"zero_cross\"");
        }
};
