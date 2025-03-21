#include "OrderValidator.hpp"

void 
OrderValidator::setParams(json configData)
{
    // Check if required keys exist in configData
    if (!configData.contains("max_position_size")) {
        throw std::runtime_error("Error: Missing 'max_position_size' in config data.");
    }
    if (!configData.contains("max_exposure")) {
        throw std::runtime_error("Error: Missing 'max_exposure' in config data.");
    }
    if (!configData.contains("slippage_tolerance")) {
        throw std::runtime_error("Error: Missing 'slippage_tolerance' in config data.");
    }

    // Now safely extract values
    maxPositionSize = configData["max_position_size"];
    maxExposure = configData["max_exposure"];
    slippageTolerance = configData["slippage_tolerance"];

    std::cout << "  --> Params set: " 
                << "maxPositionSize=" << maxPositionSize << ", "
                << "maxExposure=" << maxExposure << ", "
                << "slippageTolerance=" << slippageTolerance << std::endl;
}

bool OrderValidator::validateOrder(const Order& order, MarketData& marketData)
{
    
    
    if (!isValidOrderType(order)) return false;
    if (!isValidPrice(order, marketData)) return false;
    if (!isValidQuantity(order)) return false;
    if (!checkMaxPositionSize(order)) return false;
    if (!checkStopLoss(order, marketData)) return false;
    if (!checkTickSize(order, marketData)) return false;
    if (!checkSlippage(order, marketData)) return false;

    return true;  // Passes all checks
}

bool
OrderValidator::isValidOrderType(const Order& order)
{
    return (order.getType() == "BUY" ||
            order.getType() == "SELL");
}

bool
OrderValidator::isValidPrice(const Order& order, MarketData& marketData)
{
    return (order.getPrice() == marketData.getLastClosePrice());
}

bool
OrderValidator::isValidQuantity(const Order& order)
{
        return (order.getQuantity() > 0 &&
                order.getQuantity() <= maxPositionSize);
}

bool
OrderValidator::checkMaxPositionSize(const Order& order)
{
    return true; // TODO: Implement
    // Will need oms here
}

bool
OrderValidator::checkStopLoss(const Order& order, MarketData& marketData)
{
    // Add stop loss into strategy config
    // When creating a order from a strategy, set the stop loss, this will be a %
    // E.g. price of 100, stop loss of 10%, stop_loss_price = 90

    // TODO: Decide
    // Maybe need to have some user input here
    // If price has fallen too far maybe we dont want to do the trade?

    // This also forces us to use stoploss and take profit values which is probs a good thing

    
    return (order.getStopLossPrice() == 0 ||
            order.getStopLossPrice() != marketData.getLastClosePrice() ||
            order.getStopLossPrice() < marketData.getLastClosePrice());
}

bool
OrderValidator::checkTakeProfit(const Order& order, MarketData& marketData)
{
    // TODO: Decide
    // Maybe need to have some user input here
    // If price has rises too far maybe we dont want to do the trade?
    return (order.getTakeProfitPrice() == 0 ||
            order.getTakeProfitPrice() != marketData.getLastClosePrice() ||
            order.getTakeProfitPrice() > marketData.getLastClosePrice());
}

bool
OrderValidator::checkTickSize(const Order& order, MarketData& marketData)
{
    return true; // TODO: Implement
}

bool
OrderValidator::checkSlippage(const Order& order, MarketData& marketData) 
{
    return true; // TODO: Implement
}
