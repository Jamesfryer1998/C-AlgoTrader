#pragma once

#include <iostream>

#include "Order.hpp"
#include "Position.hpp"
#include "../data_access/MarketData.hpp"
#include "OrderValidator.hpp"
#include "../broker/BrokerBase.hpp"

using namespace std;


// Has no knowledge of MarketData, that is handled by strategy manager
class OrderManagement
{
    public:
        OrderManagement();
        ~OrderManagement();
        void removeOrder(int id);
        void addOrder(Order &order);
        void removePosition(int id);
        void onNewOrder(Order& order);
        void onOrderExecuted(Order& order);
        void addPosition(Position &position);
        vector<Order> getOrders(){ return orders; };
        void setUp(json configdata, BrokerBase* Broker);
        void reset() { orders.clear(); positions.clear(); };
        vector<Position> getPositions(){ return positions; };
        {
            validator.setParams(configdata);
            broker = Broker;
        };
        void setMarketData(MarketData marketdata) { marketData = marketdata;};

        BrokerBase* broker;
        vector<Order> orders;
        int latestOrderId = 1;
        MarketData marketData;
        int latestPositionId = 1;
        OrderValidator validator;
        vector<Position> positions;
};