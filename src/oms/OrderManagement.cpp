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
    order.setId(latestOrderId);
    orders.push_back(order);
    latestOrderId++;
}

void
OrderManagement::addPosition(Position &position)
{
    position.setId(latestPositionId);
    positions.push_back(position);
    latestPositionId++;
}

void OrderManagement::removeOrder(int id)
{
    std::erase_if(orders, [id](Order& order) { return order.getId() == id; });
}

void OrderManagement::removePosition(int id)
{
    std::erase_if(positions, [id](Position& position) { return position.getId() == id; });
}

void OrderManagement::onNewOrder(Order& order)
{
    // Wait for new order to be added to orders from strat engine
    bool orderValid = validator.validateOrder(order, marketData, positions);
    if(orderValid)
    {
        addOrder(order);

        // connect with Broker API to place order here
        if (broker->placeOrder(order))
            syncPosition(order.getTicker());
    }
}

// Ask the broker what we actually hold for this ticker and make that our one position for it.
// Called after each order, and at start-up so positions from a previous run are not forgotten.
void OrderManagement::syncPosition(const string& ticker)
{
    Position latest = broker->getLatestPosition(ticker);

    std::erase_if(positions, [&ticker](Position& position) { return position.getTicker() == ticker; });

    if (latest.getQuantity() != 0)
        addPosition(latest);
}
