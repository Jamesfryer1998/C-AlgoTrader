#pragma once

#include <iostream>
#include "../oms/Order.hpp"
#include "StrategyAttribute.hpp"
#include "../data_access/MarketData.hpp"
#include "../data_access/MarketCondition.hpp"


// class StrategyBase {
// public:
//     virtual ~StrategyBase() = default;
//     virtual void execute() = 0;  // Pure virtual function
//     static std::unique_prt<StrategyBase> create(const std::string& name);

// protected:
    // Register a strategy name with a factory function
    // static void registerStrategy(const std::string& name, std::function<std::unique_ptr<StrategyBase>()> creator);

// };

class StrategyBase
{
    public:
        StrategyBase(StrategyAttribute strategyAttribute):
        _strategyAttribute(strategyAttribute)
        {
        };

        virtual ~StrategyBase() = default;
        virtual void execute() = 0;
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

        virtual void supplyData(MarketData& marketdata)
        {
            marketData = marketdata;
        }

        // Base strats take in entire list of strat params
        // Specific strats pick and choose from this list
        Order order;
        MarketData marketData;
        bool NewOrder = false;
        StrategyAttribute _strategyAttribute;

    private:


};
