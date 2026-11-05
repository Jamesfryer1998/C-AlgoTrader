#pragma once

#include <iostream>
#include <memory>
#include <optional>
#include <vector>
#include "StrategyBase.hpp"
#include "StrategyFactory.hpp"
#include "../data_access/MarketData.hpp"

/**
 * StrategyEngine
 *
 * The strategies for one stock, and the vote between them. After each new bar:
 *
 *   std::optional<Order> order = engine.decide(heldQuantity);
 *
 *   1. collectProposals: every strategy looks at the bars and proposes BUY, SELL or nothing
 *   2. agreedOrder:      if every strategy that wants to trade agrees on the direction, that is the order;
 *                        if they disagree, nothing is traded
 *
 * The engine only decides. Sending the order (risk checks, then the broker) is the caller's job, through the
 * OrderManagement: the live app's LiveTrader::onBar and the Backtester's executeTimeStep.
 */
class StrategyEngine
{
    public:
        StrategyEngine();
        ~StrategyEngine();

        // Build the active strategies in config. marketData is the stock's bars, read by every strategy
        // (not copied: it must outlive the engine).
        void setUp(json configData, StrategyFactory &stratFactory, const MarketData &marketdata);

        // The newest bar has been added to marketData: the order the strategies agree on, if any.
        // heldQuantity: what we hold in the stock now (+ long, - short).
        std::optional<Order> decide(float heldQuantity);

        // The most bars any active strategy needs before it can decide (size the warm-up with this)
        int barsNeeded() const;

        // Each strategy's levels after the last decision (for the live app's per-minute table)
        std::vector<StrategyBase::DecisionLevels> decisionLevels() const;

        // Print each strategy's status line after it decides (default on; the live app shows a table instead)
        void setPrintStatus(bool value) { printStatus = value; }

        // Given the orders the strategies proposed for one bar, the one to send: all proposals must agree on
        // ticker and direction (all buys or all sells), and then the first is sent once (not once per
        // strategy), named after every strategy that agreed, e.g. "RSI+MACD". Nothing if they disagree.
        static std::optional<Order> agreedOrder(const std::vector<Order>& proposals);

    private:
        std::vector<Order> collectProposals(float heldQuantity);
        void printStatusLine(const StrategyBase& strategy) const;
        void printStategies();

        json configData;
        const MarketData* marketData = nullptr;
        bool printStatus = true;
        std::vector<std::unique_ptr<StrategyBase>> strategyList;
};
