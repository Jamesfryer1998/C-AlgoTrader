#pragma once

#include <memory>
#include <string>
#include <vector>
#include "../broker/BrokerBase.hpp"
#include "../data_access/MarketData.hpp"
#include "../oms/OrderManagement.hpp"
#include "../strategy_engine/Portfolio.hpp"
#include "../strategy_engine/StrategyEngine.hpp"
#include "../strategy_engine/StrategyFactory.hpp"

/**
 * StockLane
 *
 * One stock the live app trades: its own price history and strategies. Every lane sends its orders through
 * the same OMS, so orders, positions and saved trades for all stocks are in one place.
 */
class StockLane
{
    public:
        StockLane(const PortfolioEntry& entry, OrderManagement& sharedOms, BrokerBase& broker);

        const std::string& getTicker() const { return ticker; }
        MarketData& getMarketData() { return marketData; }

        // e.g. "MEANREV" or "RSI, MACD"
        std::string strategyNames() const;

        // The most bars any of its strategies needs before it can decide
        int barsNeeded() const { return engine.barsNeeded(); }

        // Load earlier bars so the strategies can decide from the first new one
        void backfill(const std::vector<MarketCondition>& history);

        // Add a new bar and let the strategies decide (they may send an order). Returns how long it took in ms.
        double decide(const MarketCondition& bar);

        // Each strategy's trigger levels after the last decision, for the table
        std::vector<StrategyBase::Levels> strategyLevels() const { return engine.levels(); }

    private:
        std::string ticker;
        json config;
        MarketData marketData;
        std::unique_ptr<StrategyFactory> factory;
        StrategyEngine engine;
        size_t barsToKeep = 1;
};
