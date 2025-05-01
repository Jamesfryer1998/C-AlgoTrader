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
        
        // Enable detailed logging for debugging
        void enableDetailedLogging(bool enable);

        // BrokerBase interface implementation
        int connect() override;
        int disconnect() override;
        int placeOrder(Order order) override;
        float getLatestPrice(std::string ticker) override;
        Position getLatestPosition(std::string ticker) override;
        
        // Simulation specific methods
        void process();
        void nextStep();
        int getStep() { return step; };
        std::string getBrokerName() { return brokerName; };
        void updateData(MarketData MarketData) { marketData = MarketData; };
        
        // Performance metrics
        double getPnL() const;
        int getNumTrades() const;
        double getDrawdown() const;
        double getCurrentEquity() const;
        double getStartingCapital() const;
        const std::vector<Order>& getFilledOrders() const;
        double getCurrentCash() const { return currentCash; }
        size_t getPendingOrdersCount() const { return pendingOrders.size(); }
        double getSlippagePercentage() const { return slippagePercentage; }
        double getCommissionPerTrade() const { return commissionPerTrade; }
        
        // Custom order handling
        void setSlippage(double slippagePerc);
        void setStartingCapital(double capital);
        void setMarketData(MarketData& marketData);
        void setCommission(double commissionPerTrade);

        
    private:
        // Order processing
        void processOrders();
        void checkStopLosses();
        void checkTakeProfits();
        void updatePortfolioValue();
        void executeOrder(Order& order);
        bool checkOrderValidity(const Order& order) const;
        void updatePositions(const Order& order, double executionPrice);
        
        // For testing
        bool useFixedSeed;
        unsigned int randomSeed;
        void setRandomSeed(unsigned int seed);
        
        // Market data
        MarketData& marketData;
        MarketCondition currentCondition;
        
        // Orders tracking
        std::vector<Order> filledOrders;
        std::vector<Order> pendingOrders;
        std::vector<Order> cancelledOrders;
        
        // Positions and portfolio
        std::vector<Position> positionHistory;
        std::map<std::string, Position> positionsByTicker;
        
        // Performance metrics
        int totalTrades;
        double currentCash;
        double currentEquity;
        double highestEquity;
        double startingCapital;
        double slippagePercentage;
        double commissionPerTrade;
        
        // Simulation state
        int step;
        bool detailedLogging;
        std::string simulationTime;
};
