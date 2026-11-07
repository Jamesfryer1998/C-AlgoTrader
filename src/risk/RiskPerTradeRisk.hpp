#pragma once

#include <cmath>
#include "RiskEngine.hpp"
#include "../util/Format.hpp"

/**
 * RISK_PER_TRADE - every trade risks the same share of the account if its stop loss is hit
 *
 *   "risk": {"name": "RISK_PER_TRADE", "risk_percent": 0.5}
 *
 *   shares = equity x risk_percent% / (price - stop loss price)
 *
 * A tight stop allows a bigger position and a wide one a smaller, so a stopped-out trade always loses about
 * risk_percent% (plus slippage). Uses the stop loss each strategy already sets; an order without one isn't
 * traded.
 */
class RiskPerTradeRisk : public RiskEngine
{
    public:
        static constexpr const char* NAME = "RISK_PER_TRADE";
        static constexpr double DEFAULT_RISK_PERCENT = 0.5;

        explicit RiskPerTradeRisk(const json& settings)
            : RiskEngine(settings), riskPercent(settings.value("risk_percent", DEFAULT_RISK_PERCENT)) {}

        std::string describe() const override { return std::string(NAME) + " " + Format::number(riskPercent, 2) + "%"; }

    protected:
        double targetShares(const Order& order, const SizingContext& context) const override
        {
            double lossPerShare = std::fabs(order.getPrice() - order.getStopLossPrice());
            if (order.getStopLossPrice() <= 0 || lossPerShare <= 0)
                return 0;
            return percentOf(context.equity, riskPercent) / lossPerShare;
        }

    private:
        double riskPercent;
};
