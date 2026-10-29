#pragma once

#include <iostream>

#include "Order.hpp"
#include "Position.hpp"
#include "../data_access/MarketData.hpp"
#include "OrderValidator.hpp"
#include "../broker/BrokerBase.hpp"
#include "PerformanceTracker.hpp"
#include "TradeStore.hpp"

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
        void syncPosition(const string& ticker);
        void addPosition(Position &position);
        vector<Order> getOrders(){ return orders; };
        void reset() { orders.clear(); positions.clear(); };
        vector<Position> getPositions(){ return positions; };
        void setUp(json configdata, BrokerBase* Broker)
        {
            validator.setParams(configdata);
            broker = Broker;
        };
        void setMarketData(const MarketData& marketdata) { marketData = &marketdata; };

        // Live trading only: save trades/positions to disk and feed the performance tracker.
        // Loads anything saved by a previous run straight away. Backtests don't call this.
        void enablePersistence(TradeStore* tradeStore, PerformanceTracker* performanceTracker);

        BrokerBase* broker;
        vector<Order> orders;
        int latestOrderId = 1;
        const MarketData* marketData = nullptr; // owned by the caller, not copied
        int latestPositionId = 1;
        OrderValidator validator;
        vector<Position> positions;

    private:
        void recordTrade(const Order& order);
        void reconcileWithBroker(const string& ticker, float brokerQuantity);

        TradeStore* store = nullptr;
        PerformanceTracker* tracker = nullptr;
};