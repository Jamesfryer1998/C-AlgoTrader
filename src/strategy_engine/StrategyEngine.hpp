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

        // The most bars any active strategy needs before it can decide (size the warm-up with this)
        int barsNeeded() const;

        // Each strategy's levels after the last run (for the live app's per-minute table)
        std::vector<StrategyBase::Levels> levels() const;

        // Print each strategy's status line after it runs (default on; the live app shows a table instead)
        void setPrintStatus(bool value) { printStatus = value; }

        // Get the order management system
        OrderManagement* getOms() { return oms; }

        // Use an OMS shared with other engines instead of this engine's own (live trading of several
        // stocks: one engine per stock, one OMS for all, so orders, positions and saved trades are in one place).
        // Call before setUp. The engine points the shared OMS at its own market data each time it runs.
        void useSharedOms(OrderManagement* shared) { oms = shared ? shared : ownOms.get(); }

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
        std::unique_ptr<OrderManagement> ownOms = std::make_unique<OrderManagement>(); // one per engine, so backtests don't share orders/positions
        OrderManagement* oms = ownOms.get();   // ownOms, or one shared with other engines
        bool printStatus = true;
        std::vector<std::unique_ptr<StrategyBase>> strategyList;
};  