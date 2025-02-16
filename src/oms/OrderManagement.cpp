#include "OrderManagement.hpp"

OrderManagement::OrderManagement() 
{
}

OrderManagement::~OrderManagement() 
{
}

void
OrderManagement::addOrder(Order &order)
{
    orders.push_back(order);
}

void
OrderManagement::addPosition(Position &position)
{
    positions.push_back(position);
}