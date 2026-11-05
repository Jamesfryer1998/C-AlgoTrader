#pragma once

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include "../oms/Order.hpp"
#include "StrategyAttribute.hpp"
#include "../data_access/MarketData.hpp"
#include "../data_access/MarketCondition.hpp"


class StrategyBase
{
    public:
        StrategyBase(StrategyAttribute strategyAttribute):
        _strategyAttribute(strategyAttribute)
        {
        };

        virtual ~StrategyBase() = default;
        virtual void execute() = 0;

        // Throws if a required setting is missing. Called once, when the factory creates the strategy.
        virtual void validate() = 0;

        // One line showing where the strategy stands against the levels that would make it trade,
        // e.g. "MEANREV  L: 205.10 | C: 206.52 | U: 209.80 ...". Empty until it has enough bars.
        virtual std::string status() const { return ""; }

        // The same as columns for the live app's per-minute table. lower / upper are the levels that would make
        // the strategy trade (or close its trade); current is the value compared with them. Empty until ready.
        struct Levels
        {
            std::string label;     // e.g. "MEANREV(60/60)"
            std::string lower;
            std::string current;
            std::string upper;
            std::string state;     // e.g. "flat, z +0.56" or "long 3/30 bars"
            bool ready = false;
        };
        virtual Levels levels() const { return Levels(); }

        // How many 1-minute bars the strategy needs before it can make a decision
        virtual int barsNeeded() const = 0;

        virtual bool onNewOrder()
        {
            return (NewOrder == true);
        };

        virtual void reset()
        {
            NewOrder = false;
        }

        virtual Order getOrder()
        {
            NewOrder = false;
            return order; 
        };

        // The position actually held in this strategy's ticker (+ long, - short), told by the engine before
        // each run. Lets a strategy notice when the broker's stop loss / take profit closed its trade.
        void setCurrentPosition(float quantity) { position = quantity; knowsPosition = true; }
        float currentPosition() const { return position; }
        bool positionKnown() const { return knowsPosition; }

        // Strategies read the engine's MarketData directly (no copy). It must outlive the strategy run.
        virtual void supplyData(const MarketData& marketdata)
        {
            marketData = &marketdata;
        }

        // Base strats take in entire list of strat params
        // Specific strats pick and choose from this list
        Order order;
        const MarketData* marketData = nullptr;
        bool NewOrder = false;
        StrategyAttribute _strategyAttribute;

        // A number with fixed decimals, for status lines and table cells
        static std::string number(double value, int decimals = 2, bool sign = false)
        {
            char text[64];
            std::snprintf(text, sizeof(text), sign ? "%+.*f" : "%.*f", decimals, value);
            return text;
        }

    protected:
        float position = 0;
        bool knowsPosition = false;

        // ---- Helpers shared by every strategy ----

        // The last `count` closing prices, oldest first. Empty if there aren't that many bars yet.
        std::vector<float> recentCloses(int count) const
        {
            std::vector<float> closes;
            if (!marketData || count <= 0)
                return closes;
            const auto& data = marketData->getData();
            if (static_cast<int>(data.size()) < count)
                return closes;
            closes.reserve(count);
            for (size_t i = data.size() - count; i < data.size(); i++)
                closes.push_back(data[i].Close);
            return closes;
        }

        // The newest bar
        const MarketCondition& currentBar() const { return marketData->getData().back(); }

        // Raise a market order at the current bar's close, with this strategy's stop loss / take profit
        void signal(OrderType type, float quantity = 1)
        {
            const MarketCondition& bar = currentBar();
            Order newOrder(type, bar.Ticker, quantity, bar.Close);
            newOrder.setStopLoss(_strategyAttribute.stop_loss);
            newOrder.setTakeProfit(_strategyAttribute.take_profit);
            order = newOrder;
            NewOrder = true;
        }
};
