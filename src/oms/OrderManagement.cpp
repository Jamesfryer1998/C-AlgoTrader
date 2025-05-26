#include <algorithm>

#include "OrderManagement.hpp"

OrderManagement::OrderManagement() 
{
}

OrderManagement::~OrderManagement() 
{
}

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
    std::cout << "We are in onNewOrder" << std::endl;
    
    // Update price from IBKR with tick price before we validate
    double latestPrice = broker->getLatestPrice("EURGBP");
    if (latestPrice!= 0) {
        // order.setPrice(latestPrice);
    }

    bool orderValid = validator.validateOrder(order, marketData, positions);
    if(orderValid)
    {
        addOrder(order);

        // connect with Broker API to place order here
        if (broker->placeOrder(order))
        {
            // oms::Position newPosition = broker->getLatestPosition(order.getTicker());
            // addPosition(newPosition);

            onOrderExecuted(order);
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