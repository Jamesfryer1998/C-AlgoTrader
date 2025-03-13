#include <algorithm>

#include "OrderManagement.hpp"

OrderManagement::OrderManagement() 
{
}

OrderManagement::~OrderManagement() 
{
}

// OrderManagement::setBroker(BrokerAPI brokerApi)
// {
//     broker = brokerApi;
// }

void
OrderManagement::addOrder(Order &order)
{
    order.id = latestOrderId;
    orders.push_back(order);
    latestOrderId++;
}

void
OrderManagement::addPosition(Position &position)
{
    position.id = latestPositionId;
    positions.push_back(position);
    latestPositionId++;
}

void OrderManagement::removeOrder(int id)
{
    std::erase_if(orders, [id](Order& order) { return order.id == id; });
}

void OrderManagement::removePosition(int id)
{
    std::erase_if(positions, [id](Position& position) { return position.id == id; });
}

void OrderManagement::onNewOrder(Order& order)
{
    // Wait for new order to be added to orders from strat engine
    addOrder(order);

    // connect with Broker API to place order here

    // BrokerAPI api;

}

        
void OrderManagement::onOrderExecuted(Order& order)
{
    // Order passes back from Broker API saying we have executed
    // Create position from order and add to positions
    // Strats will then use positions to make decisions    
    Position newPosition{
        latestPositionId,
        order.ticker,
        order.quantity,
        order.price // Broker will change this to average price 
    };

    addPosition(newPosition);
}