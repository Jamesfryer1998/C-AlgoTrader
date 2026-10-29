#pragma once

#include <iostream>
#include <memory>
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

        // Get the order management system
        OrderManagement* getOms() { return oms.get(); }

        // Given the orders every strategy proposed this bar, decide which (if any) to send.
        // All proposals must agree on ticker and direction (all buys or all sells); if they disagree,
        // nothing is sent. When they agree, the first strategy's order is sent once (not once per strategy).
        static bool chooseOrder(const std::vector<Order>& proposals, Order& chosen);

    private:
        // Execute strategies on current market data
        void executeStrategies();
        
        // Print loaded strategies for debugging
        void printStategies();

        json configData;
        MarketData* marketData; // Use a pointer to the MarketData object
        std::unique_ptr<OrderManagement> oms = std::make_unique<OrderManagement>(); // one per engine, so backtests don't share orders/positions
        std::vector<std::unique_ptr<StrategyBase>> strategyList;
};  