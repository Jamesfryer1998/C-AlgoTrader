#include "Order.hpp"

// Implement the OrderType conversion functions
string orderTypeToString(OrderType type) {
    switch (type) {
        case OrderType::BUY: return "BUY";
        case OrderType::SELL: return "SELL";
        case OrderType::LIMIT_BUY: return "LIMIT_BUY";
        case OrderType::LIMIT_SELL: return "LIMIT_SELL";
        case OrderType::STOP_BUY: return "STOP_BUY";
        case OrderType::STOP_SELL: return "STOP_SELL";
        case OrderType::HOLD: return "HOLD";
        default: return "UNKNOWN";
    }
}

OrderType stringToOrderType(const string& typeStr) {
    if (typeStr == "BUY" || typeStr == "buy") return OrderType::BUY;
    if (typeStr == "SELL" || typeStr == "sell") return OrderType::SELL;
    if (typeStr == "LIMIT_BUY" || typeStr == "limit_buy") return OrderType::LIMIT_BUY;
    if (typeStr == "LIMIT_SELL" || typeStr == "limit_sell") return OrderType::LIMIT_SELL;
    if (typeStr == "STOP_BUY" || typeStr == "stop_buy") return OrderType::STOP_BUY;
    if (typeStr == "STOP_SELL" || typeStr == "stop_sell") return OrderType::STOP_SELL;
    // For compatibility with legacy code
    if (typeStr == "Filled") return OrderType::BUY;
    
    throw std::runtime_error("Unknown order type: " + typeStr);
    return OrderType::UNKNOWN;
}

// Constructor with OrderType
oms::Order::Order(
    OrderType _type,
    string _ticker,
    float _quantity,
    float _price)
:
  type(_type),
  ticker(_ticker),
  quantity(_quantity),
  price(_price),
  stopLossPrice(0),
  takeProfitPrice(0)
{
}

// Constructor with string type (for backward compatibility)
oms::Order::Order(
    string _typeStr,
    string _ticker,
    float _quantity,
    float _price)
:
  type(stringToOrderType(_typeStr)),
  ticker(_ticker),
  quantity(_quantity),
  price(_price),
  stopLossPrice(0),
  takeProfitPrice(0)
{
}

oms::Order::~Order() 
{
}

// Can set negative percentages
void 
oms::Order::setStopLoss(float stopLossPercentage) 
{
    if (isSell()) {
        stopLossPrice = ((stopLossPercentage / 100.0) + 1.0) * price;
    } else if (isBuy()) {
        stopLossPrice = (1.0 - (stopLossPercentage / 100.0)) * price;    
    }
}

// Can set negative percentages
void 
oms::Order::setTakeProfit(float takeProfitPercentage) 
{
    if (isSell()) {
        takeProfitPrice = (1.0 - (takeProfitPercentage / 100.0)) * price;
    } else if (isBuy()) {
        takeProfitPrice = ((takeProfitPercentage / 100.0) + 1.0) * price;
    }
}