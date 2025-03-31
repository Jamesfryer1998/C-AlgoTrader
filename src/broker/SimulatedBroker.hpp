#pragma once

#include "BrokerBase.hpp"
#include "../data_access/MarketData.hpp"
#include <map>
#include <memory>
#include <string>

class SimulatedBroker : public BrokerBase {
    public:
        SimulatedBroker(MarketData& marketdata); 
        ~SimulatedBroker();
        
        // For testing - allows setting a fixed random seed for deterministic tests
        void enableFixedRandomSeed(unsigned int seed);

        // BrokerBase interface implementation
        int connect() override;
        int disconnect() override;
        float getLatestPrice(std::string ticker) override;
        int placeOrder(Order order) override;
        Position getLatestPosition(std::string ticker) override;
        
        // Simulation specific methods
        void nextStep();
        std::string getBrokerName() { return brokerName; };
        void process();
        int getStep() { return step; };
        
        // Performance metrics
        double getPnL() const;
        double getCurrentEquity() const;
        double getCurrentCash() const { return currentCash; }
        double getStartingCapital() const;
        double getDrawdown() const;
        int getNumTrades() const;
        const std::vector<Order>& getFilledOrders() const;
        size_t getPendingOrdersCount() const { return pendingOrders.size(); }
        double getSlippagePercentage() const { return slippagePercentage; }
        double getCommissionPerTrade() const { return commissionPerTrade; }
        
        // Custom order handling
        void setSlippage(double slippagePerc);
        void setCommission(double commissionPerTrade);
        void setStartingCapital(double capital);
        
    private:
        // Order processing
        void processOrders();
        bool checkOrderValidity(const Order& order) const;
        void executeOrder(Order& order);
        void updatePositions(const Order& order, double executionPrice);
        void checkStopLosses();
        void checkTakeProfits();
        void updatePortfolioValue();
        
        // For testing
        void setRandomSeed(unsigned int seed);
        bool useFixedSeed;
        unsigned int randomSeed;
        
        // Market data
        MarketCondition currentCondition;
        MarketData& marketData;
        
        // Orders tracking
        std::vector<Order> pendingOrders;
        std::vector<Order> filledOrders;
        std::vector<Order> cancelledOrders;
        
        // Positions and portfolio
        std::map<std::string, Position> positionsByTicker;
        std::vector<Position> positionHistory;
        
        // Performance metrics
        double startingCapital;
        double currentCash;
        double currentEquity;
        double highestEquity;
        double slippagePercentage;
        double commissionPerTrade;
        int totalTrades;
        
        // Simulation state
        int step;
        std::string simulationTime;
};
