#pragma once

#include <iostream>
#include "StrategyBase.hpp"
#include "../oms/OrderManagement.hpp"
#include "../data_access/MarketData.hpp"
#include "StrategyFactory.hpp"
#include "../broker/BrokerBase.hpp"


class StrategyEngine
{
    public:
        StrategyEngine();
        ~StrategyEngine();

        // Run the strategy engine for one iteration
        void run();
        
        // Set up the strategy engine
        void setUp(json configData, StrategyFactory &stratFactory, MarketData &marketdata, BrokerBase* broker);

        // Update market data
        void setMarketData(MarketData& inputData);
        
        // Get the order management system
        OrderManagement* getOms() { return oms;};

    private:
        // Execute strategies on current market data
        void executeStrategies();
        
        // Print loaded strategies for debugging
        void printStategies();

        json configData;
        MarketData* marketData; // Use a pointer to the MarketData object
        static OrderManagement* oms;
        std::vector<MarketCondition> marketConditions;
        std::vector<std::unique_ptr<StrategyBase>> strategyList;
};  