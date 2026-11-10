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
        // Risk checks, then the broker. The order is recorded (trades.csv, P&L) whether sent, rejected or blocked.
        // setMarketData must point at the order's stock first (the checks compare it with the last close).
        void sendOrder(Order& order);

        // What we hold in a stock now (+ long, - short), by our own records
        float heldQuantity(const string& ticker) const;
        void syncPosition(const string& ticker);

        // A backtest that starts holding a position (carried over from the live account): recorded as an
        // adjustment, so the records show where it came from and the P&L book knows what it cost.
        // setMarketData must point at its stock first (the record takes that stock's time).
        void recordCarriedPosition(const string& ticker, float quantity, float averagePrice);
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

        // What enablePersistence reloaded from the last run
        size_t getRestoredOrders() const { return restoredOrders; }
        size_t getRestoredPositions() const { return restoredPositions; }

        BrokerBase* broker;
        vector<Order> orders;
        int latestOrderId = 1;
        size_t restoredOrders = 0;
        size_t restoredPositions = 0;
        const MarketData* marketData = nullptr; // owned by the caller, not copied
        int latestPositionId = 1;
        OrderValidator validator;
        vector<Position> positions;

    private:
        TradeRecord makeRecord(const Order& order, int ourId) const;
        void saveTrade(const TradeRecord& trade);
        void reconcileWithBroker(const string& ticker, const Position& brokerPosition);
        void applyToPositions(const Order& order);

        TradeStore* store = nullptr;
        PerformanceTracker* tracker = nullptr;
};