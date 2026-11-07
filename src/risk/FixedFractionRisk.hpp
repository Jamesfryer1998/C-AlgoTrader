#pragma once

#include "RiskEngine.hpp"
#include "../util/Format.hpp"

/**
 * FIXED_FRACTION - each position is a set share of the account
 *
 *   "risk": {"name": "FIXED_FRACTION", "position_percent": 10}
 *
 *   shares = equity x position_percent% / price
 *
 * Positions grow as the account grows and are the same size in $ whatever the share price.
 */
class FixedFractionRisk : public RiskEngine
{
    public:
        static constexpr const char* NAME = "FIXED_FRACTION";
        static constexpr double DEFAULT_POSITION_PERCENT = 10.0;

        explicit FixedFractionRisk(const json& settings)
            : RiskEngine(settings), positionPercent(settings.value("position_percent", DEFAULT_POSITION_PERCENT)) {}

        std::string describe() const override { return std::string(NAME) + " " + Format::number(positionPercent, 1) + "%"; }

    protected:
        double targetShares(const Order& order, const SizingContext& context) const override
        {
            return percentOf(context.equity, positionPercent) / order.getPrice();
        }

    private:
        double positionPercent;
};
