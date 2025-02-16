#include "Order.hpp"


Order::Order(
    int _id,
    string _type,
    string _ticker,
    float _quantity,
    float _price)

: id(_id),
  type(_type),
  ticker(_ticker),
  quantity(_quantity),
  price(_price)
{
}

Order::~Order() 
{
}