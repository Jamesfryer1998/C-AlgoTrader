#include <algorithm>

#include "OrderManagement.hpp"

// OrderValidator* OrderManagement::validator = new OrderValidator();

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
OrderManagement::addOrder(oms::Order &order)
{
    order.setId(latestOrderId);
    orders.push_back(order);
    latestOrderId++;
}

void
OrderManagement::addPosition(oms::Position &position)
{
    position.setId(latestPositionId);
    positions.push_back(position);
    latestPositionId++;
}

void OrderManagement::removeOrder(int id)
{
    std::erase_if(orders, [id](oms::Order& order) { return order.getId() == id; });
}

void OrderManagement::removePosition(int id)
{
    std::erase_if(positions, [id](oms::Position& position) { return position.getId() == id; });
}

void OrderManagement::onNewOrder(oms::Order& order)
{
    // Wait for new order to be added to orders from strat engine
    bool orderValid = validator.validateOrder(order, marketData, positions);
    if(orderValid)
    {
        addOrder(order);

        // connect with Broker API to place order here
        if (broker->placeOrder(order))
        {
            oms::Position newPosition = broker->getLatestPosition(order.getTicker());
            addPosition(newPosition);
        }
    }
}

        
void OrderManagement::onOrderExecuted(oms::Order& order)
{
    // oms::Order passes back from Broker API saying we have executed
    // Create position from order and add to positions
    // Strats will then use positions to make decisions    
    oms::Position newPosition{
        order.getTicker(),
        order.getQuantity(),
        order.getPrice() // Broker will change this to average price 
    };

    addPosition(newPosition);
}