#pragma once

#include "StrategyBase.hpp"
#include "../util/Log.hpp"
#include <iomanip>
#include <sstream>

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

        // A long window so the moving averages have settled
        int barsNeeded() const override { return 5 * (_strategyAttribute.long_period + _strategyAttribute.signal_period); }

        void execute() override
        {
            std::vector<float> closes = recentCloses(barsNeeded());
            if (closes.empty())
                return;   // not enough bars yet

            Values v = calculate(closes, _strategyAttribute.short_period,
                                 _strategyAttribute.long_period, _strategyAttribute.signal_period);
            last = v;
            ready = true;

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

        // crossover:  "MACD(12/26/9)   MACD 0.1234 | signal 0.0987 | gap +0.0247   above: SELL if it crosses below"
        // zero_cross: "MACD(12/26/9)   MACD 0.1234 | zero line   above: SELL if it crosses below 0"
        std::string status() const override
        {
            if (!ready)
                return "";
            bool zeroCross = _strategyAttribute.signal_type == "zero_cross";
            double line = zeroCross ? 0.0 : last.signal;
            double gap = last.macd - line;
            std::ostringstream out;
            out << std::fixed << std::setprecision(4)
                << "MACD(" << _strategyAttribute.short_period << "/" << _strategyAttribute.long_period << "/"
                << _strategyAttribute.signal_period << ")   MACD " << last.macd;
            if (zeroCross)
                out << " | zero line";
            else
                out << " | signal " << last.signal << " | gap " << (gap >= 0 ? "+" : "") << gap;
            out << "   " << (gap >= 0 ? "above: SELL if it crosses below" : "below: BUY if it crosses above")
                << (zeroCross ? " 0" : " the signal line");
            return out.str();
        }

        // MACD against the line it must cross (the signal line, or 0 for zero_cross): the line sits in the
        // column it would be crossed from, Upper while MACD is below it (BUY on crossing up), Lower while above
        Levels levels() const override
        {
            Levels l;
            l.label = "MACD(" + std::to_string(_strategyAttribute.short_period) + "/" +
                      std::to_string(_strategyAttribute.long_period) + "/" + std::to_string(_strategyAttribute.signal_period) + ")";
            if (!ready)
                return l;
            bool zeroCross = _strategyAttribute.signal_type == "zero_cross";
            double line = zeroCross ? 0.0 : last.signal;
            l.ready = true;
            l.current = number(last.macd, 4);
            (last.macd >= line ? l.lower : l.upper) = number(line, 4);
            l.state = std::string(last.macd >= line ? "above " : "below ") + (zeroCross ? "0" : "signal line");
            return l;
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

    private:
        Values last;
        bool ready = false;
};
