#include "RiskEngine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    // Defaults for settings missing from the "risk" block
    const double DEFAULT_MAX_STOCK_PERCENT = 20.0;       // of equity in any one stock
    const double DEFAULT_MAX_PORTFOLIO_PERCENT = 100.0;  // of equity across every stock: no borrowing
    const double NO_SHARE_LIMIT = std::numeric_limits<double>::max();
}

RiskEngine::RiskEngine(const json& settings)
    : settings(settings),
      maxStockPercent(settings.value("max_stock_percent", DEFAULT_MAX_STOCK_PERCENT)),
      maxPortfolioPercent(settings.value("max_portfolio_percent", DEFAULT_MAX_PORTFOLIO_PERCENT)),
      maxShares(settings.value("max_position_size", NO_SHARE_LIMIT))
{
}

std::optional<Order>
RiskEngine::size(const Order& order, const SizingContext& context) const
{
    float held = context.heldQuantity;
    bool againstPosition = held != 0 && order.isBuy() != (held > 0);

    Order sized = order;
    if (againstPosition)
    {
        sized.setQuantity(std::fabs(held));   // close it all
        return sized;
    }
    if (held != 0)
        return std::nullopt;   // already in this trade at full size: a repeated signal doesn't add to it

    // Whole shares: brackets (stop loss / take profit) can't be fractional
    double shares = std::floor(cappedTarget(order, context));
    if (shares < 1)
        return std::nullopt;
    sized.setQuantity(static_cast<float>(shares));
    return sized;
}

double
RiskEngine::cappedTarget(const Order& order, const SizingContext& context) const
{
    double price = order.getPrice();
    if (price <= 0 || context.equity <= 0)
        return 0;

    double target = std::max(0.0, targetShares(order, context));
    double stockLimit = percentOf(context.equity, maxStockPercent) / price;
    double portfolioRoom = (percentOf(context.equity, maxPortfolioPercent) - context.otherPositionsValue) / price;
    return std::min({target, stockLimit, std::max(0.0, portfolioRoom), maxShares});
}
