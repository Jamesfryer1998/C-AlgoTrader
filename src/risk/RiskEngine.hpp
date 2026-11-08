#pragma once

#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../data_access/MarketData.hpp"
#include "../oms/Order.hpp"

using json = nlohmann::json;

/**
 * RiskEngine
 *
 * Decides how many shares an order is for. Strategies only say BUY or SELL; the stock's risk engine turns
 * that into a size before the OMS checks and sends it:
 *
 *   std::optional<Order> sized = risk.size(order, context);   // nothing: don't trade
 *
 * Each engine says how big a position it wants (targetShares). The base class turns that into an order:
 *
 *   - an order against the position we hold closes all of it
 *   - an order from flat opens a position of the target size; one in the direction we already hold is ignored
 *     (the position was opened at full size, and topping it up as the price moves would mean many tiny orders)
 *   - the target is capped: max_stock_percent of equity in one stock (default 20) and max_portfolio_percent
 *     across all stocks (default 100: no borrowing); and max_position_size shares if the "risk" block sets one.
 *     config/risk.json's max_position_size only limits FIXED_SIZE.
 *
 * One engine sizes every stock, set in config/risk.json:
 *
 *   "risk": {"name": "RISK_PER_TRADE", "risk_percent": 0.5, "max_stock_percent": 20, "max_portfolio_percent": 100}
 *
 * Each stock gets its own copy (they share no state), built from that block.
 *
 * Engines: FIXED_SIZE (the default: every order is "quantity" shares, as before), FIXED_FRACTION,
 * RISK_PER_TRADE, VOLATILITY_TARGET, KELLY. See each class.
 */
class RiskEngine
{
    public:
        // What an engine knows when sizing an order
        struct SizingContext
        {
            double equity = 0;                // the account's value now
            float heldQuantity = 0;           // in this stock (+ long, - short)
            double otherPositionsValue = 0;   // every other stock's position, at its last price (shorts at full size)
            const MarketData* bars = nullptr; // this stock's recent bars, newest last
            const std::vector<double>* closedTradeReturns = nullptr;   // this stock's closed trades (0.01 = +1%), oldest first
        };

        explicit RiskEngine(const json& settings);
        virtual ~RiskEngine() = default;

        // The order with its quantity set, or nothing if there's nothing to do (already in the trade, or a cap
        // leaves no room)
        virtual std::optional<Order> size(const Order& order, const SizingContext& context) const;

        // e.g. "RISK_PER_TRADE 0.5%"
        virtual std::string describe() const = 0;

    protected:
        // The position this engine wants for a new trade, in shares (before the caps)
        virtual double targetShares(const Order& order, const SizingContext& context) const = 0;

        static double percentOf(double value, double percent) { return value * percent / PERCENT; }

        static constexpr double PERCENT = 100.0;

        json settings;
        double maxStockPercent;
        double maxPortfolioPercent;
        double maxShares;

    private:
        double cappedTarget(const Order& order, const SizingContext& context) const;
};
