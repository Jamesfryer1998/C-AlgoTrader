#include "Order.hpp"


Order::Order(
    string _type,
    string _ticker,
    float _quantity,
    float _price)

:
  type(_type),
  ticker(_ticker),
  quantity(_quantity),
  price(_price)
{
}

Order::~Order() 
{
}

// Can set negative percentages
void 
Order::setStopLoss(float stopLossPercentage) 
{
    if(type == "SELL")
        stopLossPrice = ((stopLossPercentage / 100.0) + 1.0) * price;
    else if(type == "BUY")
        stopLossPrice = (1.0 - (stopLossPercentage / 100.0)) * price;    
}

// Can set negative percentages
void 
Order::setTakeProfit(float takeProfitPercentage) 
{
    if(type == "SELL")
        takeProfitPrice = (1.0 -(takeProfitPercentage / 100.0)) * price;
    else if(type == "BUY")
        takeProfitPrice = ((takeProfitPercentage / 100.0) + 1.0) * price;
}