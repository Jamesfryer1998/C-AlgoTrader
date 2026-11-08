#pragma once

#include <cstdio>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include "../oms/Order.hpp"
#include "StrategyAttribute.hpp"
#include "../data_access/MarketData.hpp"
#include "../data_access/MarketCondition.hpp"


/**
 * StrategyBase
 *
 * A strategy looks at the bars after each new one and says whether it wants to trade:
 *
 *   std::optional<Order> order = strategy.onBar(marketData, heldQuantity);
 *
 * It never sends anything itself: the engine collects every strategy's proposal, checks they agree, and the
 * caller sends the agreed order through the OMS. Each strategy writes its rule in decide(), calling
 * propose(BUY / SELL) when it wants to trade.
 */
class StrategyBase
{
    public:
        StrategyBase(StrategyAttribute strategyAttribute):
        _strategyAttribute(strategyAttribute)
        {
        };

        virtual ~StrategyBase() = default;

        // The newest bar has arrived: the order this strategy proposes, if any.
        // heldQuantity: what we hold in the stock now (+ long, - short), so a strategy can notice the broker's
        // stop loss / take profit closed its trade. data must outlive the call (strategies read it, no copy).
        std::optional<Order> onBar(const MarketData& data, float heldQuantity)
        {
            marketData = &data;
            position = heldQuantity;
            knowsPosition = true;
            proposal.reset();
            decide();
            return proposal;
        }

        // Throws if a required setting is missing. Called once, when the factory creates the strategy.
        virtual void validate() = 0;

        // One line showing where the strategy stands against the levels that would make it trade,
        // e.g. "MEANREV  L: 205.10 | C: 206.52 | U: 209.80 ...". Empty until it has enough bars.
        virtual std::string status() const { return ""; }

        // The same as columns for the live app's per-minute table. lower / upper are the levels that would make
        // the strategy trade (or close its trade); current is the value compared with them. Empty until ready.
        struct DecisionLevels
        {
            std::string label;     // e.g. "MEANREV(60/60)"
            std::string lower;
            std::string current;
            std::string upper;
            std::string state;     // e.g. "flat, z +0.56" or "long 3/30 bars"
            bool ready = false;
        };
        virtual DecisionLevels decisionLevels() const { return DecisionLevels(); }

        // How many 1-minute bars the strategy needs before it can make a decision
        virtual int barsNeeded() const = 0;

        // Base strats take in entire list of strat params
        // Specific strats pick and choose from this list
        StrategyAttribute _strategyAttribute;

    protected:
        // Each strategy's rule: look at the bars (marketData) and call propose() to trade
        virtual void decide() = 0;

        // What we held when this bar arrived (+ long, - short)
        float currentPosition() const { return position; }
        bool positionKnown() const { return knowsPosition; }

        const MarketData* marketData = nullptr;
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

        // Ask for a market order at the current bar's close, with this strategy's stop loss / take profit
        void propose(OrderType type, float quantity = 1)
        {
            const MarketCondition& bar = currentBar();
            Order order(type, bar.Ticker, quantity, bar.Close);
            order.setStopLoss(_strategyAttribute.stop_loss);
            order.setTakeProfit(_strategyAttribute.take_profit);
            order.setStrategy(_strategyAttribute.name);
            proposal = order;
        }

        // The order proposed on this bar so far (e.g. to describe it in a log line)
        const Order& proposed() const { return *proposal; }

    private:
        std::optional<Order> proposal;
};
