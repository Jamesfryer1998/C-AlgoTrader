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
        void addOrder(oms::Order &order);
        void removePosition(int id);
        void onNewOrder(oms::Order& order);
        void onOrderExecuted(oms::Order& order);
        void addPosition(oms::Position &position);
        vector<oms::Order> getOrders(){ return orders; };
        void reset() { orders.clear(); positions.clear(); };
        vector<oms::Position> getPositions(){ return positions; };
        void setUp(json configdata, BrokerBase* Broker)
        {
            validator.setParams(configdata);
            broker = Broker;
        };
        void setMarketData(MarketData marketdata) { marketData = marketdata;};

        BrokerBase* broker;
        vector<oms::Order> orders;
        int latestOrderId = 1;
        MarketData marketData;
        int latestPositionId = 1;
        OrderValidator validator;
        vector<oms::Position> positions;
};