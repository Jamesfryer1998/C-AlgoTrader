#pragma once

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

    protected:
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
