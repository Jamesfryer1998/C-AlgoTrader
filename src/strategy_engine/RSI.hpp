#pragma once

#include "StrategyBase.hpp"
#include "../util/Log.hpp"
#include <cmath>
#include <iomanip>

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
            std::vector<float> closes = recentCloses(_strategyAttribute.period);
            if (closes.empty())
                return;   // not enough bars yet

            rsi = calculateRSI(closes);

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

            Log::out() << "INFO - calculateRSI: Calculated RSI(" << _strategyAttribute.period << "): " << std::fixed
                       << std::setprecision(2) << value << " using " << closes.size() << " data points" << std::endl;

            return std::round(value * 100) / 100;
        }

    private:
        void logDecision()
        {
            const MarketCondition& bar = currentBar();
            Log::out() << "\n==== RSI SIGNAL ====" << std::endl
                       << "Date: " << bar.DateTime << std::endl
                       << "Ticker: " << bar.Ticker << std::endl
                       << "Price:  $" << std::fixed << std::setprecision(2) << bar.Close << std::endl
                       << "Quantity:  " << std::fixed << std::setprecision(2) << order.getQuantity() << std::endl
                       << "RSI:    " << std::fixed << std::setprecision(2) << rsi << std::endl
                       << "Action: " << order.getTypeAsString() << std::endl
                       << "===================\n" << std::endl;
        }

        float rsi = 50.0f;
};
