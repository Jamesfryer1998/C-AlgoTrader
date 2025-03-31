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
        void addOrder(Order &order);
        void addPosition(Position &position);
        vector<Order> getOrders(){ return orders; };
        vector<Position> getPositions(){ return positions; };
        void removeOrder(int id);
        void removePosition(int id);
        void reset() { orders.clear(); positions.clear(); };
        void onNewOrder(Order& order);
        void onOrderExecuted(Order& order);
        void setUp(json configdata, BrokerBase* Broker) 
        {
            validator.setParams(configdata);
            broker = Broker;
        };
        void setMarketData(MarketData marketdata) { marketData = marketdata;};

        int latestOrderId = 1;
        int latestPositionId = 1;
        vector<Order> orders;
        vector<Position> positions;
        MarketData marketData;
        OrderValidator validator;
        BrokerBase* broker;
};