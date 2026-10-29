#pragma once

#include <iostream>
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

    private:


};
