#pragma once

#include "RiskEngine.hpp"

/**
 * FIXED_SIZE - every order is the same number of shares (how the app traded before risk engines)
 *
 *   "risk": {"name": "FIXED_SIZE", "quantity": 1}
 *
 * Orders aren't resized: a strategy that buys three times builds a position of three, and a sell sells one.
 * The OMS's max_position_size still caps the position. The default when a stock has no "risk".
 */
class FixedSizeRisk : public RiskEngine
{
    public:
        static constexpr const char* NAME = "FIXED_SIZE";
        static constexpr float DEFAULT_QUANTITY = 1;

        explicit FixedSizeRisk(const json& settings)
            : RiskEngine(settings), quantity(settings.value("quantity", DEFAULT_QUANTITY)) {}

        std::optional<Order> size(const Order& order, const SizingContext&) const override
        {
            Order sized = order;
            sized.setQuantity(quantity);
            return sized;
        }

        std::string describe() const override { return std::string(NAME) + " " + std::to_string(static_cast<int>(quantity)); }

    protected:
        double targetShares(const Order&, const SizingContext&) const override { return quantity; }

    private:
        float quantity;
};
