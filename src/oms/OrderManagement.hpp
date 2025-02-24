#include <iostream>

#include "../data_access/MarketCondition.hpp"
#include "Order.hpp"
#include "Position.hpp"

using namespace std;


// Has no knowledge of MarketData, that is handled by strategy manager
class OrderManagement
{
    public:
        OrderManagement();
        ~OrderManagement();
        void addOrder(Order &order);
        void addPosition(Position &position);
        vector<Order> getOrders(){ return orders; };
        vector<Position> getPositions(){ return positions; };
        void removeOrder(int id);
        void removePosition(int id);
        void reset() { orders.clear(); positions.clear(); };

    private:

        void onNewOrder(Order& order);
        void onOrderExecuted(Order& order);

        int latestOrderId = 1;
        int latestPositionId = 1;
        vector<Order> orders;
        vector<Position> positions;
};