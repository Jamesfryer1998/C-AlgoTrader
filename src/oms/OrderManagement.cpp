#include <algorithm>

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

void OrderManagement::removeOrder(int id)
{
    std::erase_if(orders, [id](Order& order) { return order.getId() == id; });
}

void OrderManagement::removePosition(int id)
{
    std::erase_if(positions, [id](Position& position) { return position.getId() == id; });
}