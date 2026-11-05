#pragma once

#include "StrategyBase.hpp"
#include "../util/Log.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

/**
 * RSI - Relative Strength Index
 *
 * RSI = 100 - 100 / (1 + average gain / average loss) over the last `period` closes.
 *   RSI above overbought_threshold -> SELL (price has risen a lot, expect a fall)
 *   RSI below oversold_threshold   -> BUY  (price has fallen a lot, expect a bounce)
 * A signal is raised on every bar the RSI stays in either zone.
 */
class RSI : public StrategyBase {
    public:
        RSI(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) {}

        void execute() override
        {
            std::vector<float> closes = recentCloses(barsNeeded());
            if (closes.empty())
                return;   // not enough bars yet

            rsi = calculateRSI(closes);
            ready = true;

            if (rsi > _strategyAttribute.overbought_threshold)
                signal(OrderType::SELL);
            else if (rsi < _strategyAttribute.oversold_threshold)
                signal(OrderType::BUY);
            else
                return;

            logDecision();
        }

        void validate() override
        {
            if(!_strategyAttribute.period)
                throw std::runtime_error("RSI period not set");
            if(!_strategyAttribute.overbought_threshold)
                throw std::runtime_error("RSI overbought threshold not set");
            if(!_strategyAttribute.oversold_threshold)
                throw std::runtime_error("RSI oversold threshold not set");
        }

        const MarketData& getData() const { return *marketData; }

        float calculateRSI(const std::vector<float>& closes)
        {
            if (closes.size() <= 1)
                return 50.0f;   // neutral

            float gainSum = 0.0f;
            float lossSum = 0.0f;
            for (size_t i = 1; i < closes.size(); i++) {
                float change = closes[i] - closes[i-1];
                if (change > 0)
                    gainSum += change;
                else if (change < 0)
                    lossSum -= change;
            }

            float avgGain = gainSum / (closes.size() - 1);
            float avgLoss = lossSum / (closes.size() - 1);

            if (avgLoss == 0.0f && avgGain == 0.0f)
                return 50.0f;    // no movement
            if (avgLoss == 0.0f)
                return 100.0f;   // only gains

            float rs = avgGain / avgLoss;
            float value = 100.0f - (100.0f / (1.0f + rs));
            return std::round(value * 100) / 100;
        }

        int barsNeeded() const override { return _strategyAttribute.period; }

        // e.g. "RSI(21)   L: 35.00 | C: 48.21 | U: 80.00   BUY below L, SELL above U"
        std::string status() const override
        {
            if (!ready)
                return "";
            std::ostringstream out;
            out << std::fixed << std::setprecision(2)
                << "RSI(" << _strategyAttribute.period << ")   L: " << _strategyAttribute.oversold_threshold
                << " | C: " << rsi << " | U: " << _strategyAttribute.overbought_threshold
                << "   BUY below L, SELL above U";
            if (rsi < _strategyAttribute.oversold_threshold) out << "  <- BUY zone";
            if (rsi > _strategyAttribute.overbought_threshold) out << "  <- SELL zone";
            return out.str();
        }

        Levels levels() const override
        {
            Levels l;
            l.label = "RSI(" + std::to_string(_strategyAttribute.period) + ")";
            if (!ready)
                return l;
            l.ready = true;
            l.lower = number(_strategyAttribute.oversold_threshold);
            l.current = number(rsi);
            l.upper = number(_strategyAttribute.overbought_threshold);
            l.state = rsi < _strategyAttribute.oversold_threshold ? "in BUY zone"
                    : rsi > _strategyAttribute.overbought_threshold ? "in SELL zone" : "";
            return l;
        }

    private:
        void logDecision()
        {
            Log::out() << "RSI " << order.getTypeAsString() << " | RSI " << std::fixed << std::setprecision(2) << rsi
                       << " (oversold " << _strategyAttribute.oversold_threshold << ", overbought "
                       << _strategyAttribute.overbought_threshold << ")" << std::endl;
        }

        float rsi = 50.0f;
        bool ready = false;
};
