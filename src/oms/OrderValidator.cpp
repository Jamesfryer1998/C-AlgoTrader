#include "OrderValidator.hpp"

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

    std::cout << "  --> Params set: " 
                << "maxPositionSize=" << maxPositionSize << ", "
                << "maxExposure=" << maxExposure << ", "
                << "slippageTolerance=" << slippageTolerance << std::endl;
}

bool OrderValidator::validateOrder(const Order& order, MarketData& marketData, std::vector<Position>& positions)
{
    float totalHeldPositions = getTotalHeldQuantity(order, positions);
    
    if(!isValidOrderType(order)) return false;
    if(!isValidPrice(order, marketData)) return false;
    if(!isValidQuantity(order)) return false;
    if(!checkMaxPositionSize(order, totalHeldPositions)) return false;
    if(!checkStopLoss(order, marketData)) return false;
    if(!checkTakeProfit(order, marketData)) return false;
    // if(!checkTickSize(order, marketData))) return false;
    if(!checkSlippage(order, marketData)) return false;

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

bool OrderValidator::isValidPrice(const Order& order, MarketData& marketData)
{
    double orderPrice = order.getPrice();
    double lastClose = marketData.getLastClosePrice();

    // Calculate allowed price range based on slippage tolerance
    double allowedSlippage = (slippageTolerance / 100.0) * lastClose;
    
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
    float newTotal = totalHeldQuantity + (order.isBuy() ? order.getQuantity() : 0.0f);

    return (newTotal <= maxPositionSize);
    return (order.getQuantity() <= maxPositionSize);
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
OrderValidator::checkTakeProfit(const Order& order, MarketData& marketData)
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

bool
OrderValidator::checkTickSize(const Order& order, MarketData& marketData)
{
    return true; // TODO: Implement
    // Will need to test with certain exchanges
    // Tick size will also change based on the commondity/security we trade
}

bool OrderValidator::checkSlippage(const Order& order, MarketData& marketData) 
{
    double orderPrice = order.getPrice();
    double lastClose = marketData.getLastClosePrice();
    
    double slippage = std::abs(orderPrice - lastClose) / ((orderPrice + lastClose) / 2) * 100.0;
    std::cerr<<slippage<<std::endl;

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
