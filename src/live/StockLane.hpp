#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../data_access/MarketData.hpp"
#include "../risk/RiskEngine.hpp"
#include "../strategy_engine/Portfolio.hpp"
#include "../strategy_engine/StrategyEngine.hpp"
#include "../strategy_engine/StrategyFactory.hpp"

/**
 * StockLane
 *
 * One stock the live app trades: its price history and its strategies. A lane only decides; LiveTrader sends
 * what it decides through the one OMS every lane shares.
 */
class StockLane
{
    public:
        explicit StockLane(const PortfolioEntry& entry);

        const std::string& getTicker() const { return ticker; }
        const MarketData& getMarketData() const { return marketData; }

        // e.g. "MEANREV" or "RSI, MACD"
        std::string strategyNames() const;

        // The most bars any of its strategies needs before it can decide
        int barsNeeded() const { return engine.barsNeeded(); }

        // Load earlier bars so the strategies can decide from the first new one
        void backfill(const std::vector<MarketCondition>& history);

        // The stock's newest bar (only the bars the strategies need are kept)
        void addBar(const MarketCondition& bar);

        // The strategies vote on the newest bar: the order they agree on, if any.
        // heldQuantity: what we hold in this stock now (+ long, - short).
        std::optional<Order> decide(float heldQuantity) { return engine.decide(heldQuantity); }

        // How many shares: the risk engine's size for the order (nothing if there's nothing to do)
        std::optional<Order> size(const Order& order, RiskEngine::SizingContext context) const
        {
            context.bars = &marketData;
            return risk->size(order, context);
        }

        std::string describeRisk() const { return risk->describe(); }

        // Each strategy's trigger levels after the last decision, for the table
        std::vector<StrategyBase::DecisionLevels> decisionLevels() const { return engine.decisionLevels(); }

    private:
        std::string ticker;
        json config;
        MarketData marketData;
        std::unique_ptr<StrategyFactory> factory;
        StrategyEngine engine;
        std::unique_ptr<RiskEngine> risk;
        size_t barsToKeep = 1;
};
