#include <cmath>
#include <cstdio>
#include "OrderValidator.hpp"
#include "../util/Log.hpp"

namespace
{
    // slippage_tolerance in config.json is a percentage
    const double PERCENT = 100.0;
}

void 
OrderValidator::setParams(json configData)
{
    // Check if required keys exist in configData
    try {
        maxPositionSize = configData.at("max_position_size").get<int>();
        maxExposure = configData.at("max_exposure").get<double>();
        slippageTolerance = configData.at("slippage_tolerance").get<double>();
    } catch (const json::exception& e) {
        throw std::runtime_error("Error: Invalid config data - " + std::string(e.what()));
    }

    Log::out() << "  --> Params set: " 
                << "maxPositionSize=" << maxPositionSize << ", "
                << "maxExposure=" << maxExposure << ", "
                << "slippageTolerance=" << slippageTolerance << std::endl;
}

bool OrderValidator::validateOrder(const Order& order, const MarketData& marketData, std::vector<Position>& positions)
{
    float totalHeldPositions = getTotalHeldQuantity(order, positions);
    lastFailure.clear();

    auto fail = [this](const std::string& reason) { lastFailure = reason; return false; };
    auto num = [](double value) { char text[32]; std::snprintf(text, sizeof(text), "%g", value); return std::string(text); };
    std::string price = num(order.getPrice());
    std::string close = num(marketData.getLastClosePrice());

    if(!isValidOrderType(order))
        return fail("order type " + order.getTypeAsString() + " not allowed");
    if(!isValidPrice(order, marketData))
        return fail("price " + price + " too far from last close " + close);
    if(!isValidQuantity(order))
        return fail("quantity " + num(order.getQuantity()) + " not between 0 and max_position_size " + num(maxPositionSize));
    if(!checkMaxPositionSize(order, totalHeldPositions))
        return fail("holding " + num(totalHeldPositions) + ", " + (order.isBuy() ? "buying " : "selling ") + num(order.getQuantity())
                    + " would take the position beyond max_position_size " + num(maxPositionSize));
    if(!checkStopLoss(order, marketData))
        return fail("stop loss " + num(order.getStopLossPrice()) + " missing or on the wrong side of " + close);
    if(!checkTakeProfit(order, marketData))
        return fail("take profit " + num(order.getTakeProfitPrice()) + " missing or on the wrong side of " + close);
    if(!checkSlippage(order, marketData))
        return fail("slippage between price " + price + " and last close " + close + " above slippage_tolerance");

    return true;  // Passes all checks
}

bool
OrderValidator::isValidOrderType(const Order& order)
{
    // Check if the order type is recognized
    OrderType type = order.getType();
    return (type == OrderType::BUY || 
            type == OrderType::SELL ||
            type == OrderType::LIMIT_BUY || 
            type == OrderType::LIMIT_SELL || 
            type == OrderType::STOP_BUY || 
            type == OrderType::STOP_SELL);
}

bool OrderValidator::isValidPrice(const Order& order, const MarketData& marketData)
{
    double orderPrice = order.getPrice();
    double lastClose = marketData.getLastClosePrice();

    // Calculate allowed price range based on slippage tolerance
    double allowedSlippage = (slippageTolerance / PERCENT) * lastClose;
    
    if (order.isBuy()) {
        return orderPrice <= lastClose + allowedSlippage;
    } 
    else if (order.isSell()) {
        return orderPrice >= lastClose - allowedSlippage;
    }

    return false;
}

bool
OrderValidator::isValidQuantity(const Order& order)
{
        return (order.getQuantity() > 0 &&
                order.getQuantity() <= maxPositionSize);
}

bool
OrderValidator::checkMaxPositionSize(const Order& order, float totalHeldQuantity)
{
    // Position after this order: + long, - short
    float signedQuantity = order.isBuy() ? order.getQuantity() : -order.getQuantity();
    float after = totalHeldQuantity + signedQuantity;

    // An order that makes the position smaller is always allowed (e.g. selling when over the limit)
    if (std::fabs(after) < std::fabs(totalHeldQuantity))
        return true;

    // Otherwise the position must stay within the limit, long or short
    return std::fabs(after) <= maxPositionSize;
}

bool
OrderValidator::checkStopLoss(const Order& order, const MarketData& marketData)
{
    // Add stop loss into strategy config
    // When creating a order from a strategy, set the stop loss, this will be a %
    // E.g. price of 100, stop loss of 10%, stop_loss_price = 90

    // TODO: Decide
    // Maybe need to have some user input here
    // If price has fallen too far maybe we dont want to do the trade?

    // This also forces us to use stoploss and take profit values which is probs a good thing
    if(order.getStopLossPrice() == 0) return false;
    if(order.getStopLossPrice() == marketData.getLastClosePrice()) return false;

    if(order.getType() == OrderType::BUY)
    {
        if(order.getStopLossPrice() > marketData.getLastClosePrice()) return false;
    }
    else if(order.getType() == OrderType::SELL)
    {
        if(order.getStopLossPrice() < marketData.getLastClosePrice()) return false;
    }

    return true;
}

bool
OrderValidator::checkTakeProfit(const Order& order, const MarketData& marketData)
{
    // TODO: Decide
    // Maybe need to have some user input here
    // If price has rises too far maybe we dont want to do the trade?
    if(order.getTakeProfitPrice() == 0) return false;
    if(order.getTakeProfitPrice() == marketData.getLastClosePrice()) return false;

    if(order.getType() == OrderType::BUY)
    {
        if(order.getTakeProfitPrice() < marketData.getLastClosePrice()) return false;
    }
    else if(order.getType() == OrderType::SELL)
    {
        if(order.getTakeProfitPrice() > marketData.getLastClosePrice()) return false;
    }

    return true;
}

bool OrderValidator::checkSlippage(const Order& order, const MarketData& marketData) 
{
    double orderPrice = order.getPrice();
    double lastClose = marketData.getLastClosePrice();
    
    double slippage = std::abs(orderPrice - lastClose) / ((orderPrice + lastClose) / 2) * PERCENT;

    return slippage <= slippageTolerance;
}
float OrderValidator::getTotalHeldQuantity(const Order& order, std::vector<Position>& positions)
{
    float totalHeldQuantity = 0.0f;

    for (const auto& pos : positions) {
        if (pos.getTicker() == order.getTicker()) {
            totalHeldQuantity += pos.getQuantity();
        }
    }

    return totalHeldQuantity;
}
