#pragma once

#include "BrokerBase.hpp"
#include "../data_access/MarketData.hpp"
#include "../util/Log.hpp"
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

        // An order placed for this ticker that hasn't been filled yet (fills happen in nextStep)
        bool hasPendingOrder(const std::string& ticker) override
        {
            for (const auto& order : pendingOrders)
                if (order.getTicker() == ticker)
                    return true;
            return false;
        }
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
        
        // How slippage is applied to each fill:
        //   Random  - uniformly random between -slippage and +slippage (the original behaviour)
        //   Adverse - always against us: buys fill slippage higher, sells slippage lower (like paying the spread)
        enum class SlippageMode { Random, Adverse };
        void setSlippageMode(SlippageMode mode) { slippageMode = mode; }

        // true: each step uses the newest bar in MarketData (for a rolling window of bars, as the
        // Backtester and live trading use). false: step N uses bar N (the original behaviour).
        void followLatestBar(bool follow) { useLatestBar = follow; }

        // Several stocks in one simulation, their bars arriving one after another (the live app's replay
        // mode): an order fills, and a position's stop loss / take profit is checked, only on its own
        // stock's bar, and each stock is valued at its own last price.
        void tradeSeveralStocks(bool several) { severalStocks = several; }

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
        bool executeOrder(Order& order); // false = not filled yet (limit price not reached)
        bool checkOrderValidity(const Order& order) const;
        void updatePositions(const Order& order, double executionPrice);
        
        // For testing
        bool useFixedSeed;
        unsigned int randomSeed;
        
        // Market data
        MarketData& marketData;
        MarketCondition currentCondition;
        
        // Orders tracking
        std::vector<Order> filledOrders;
        std::vector<Order> pendingOrders;
        std::vector<Order> cancelledOrders;
        
        // Stop loss / take profit prices for each OPEN position (from the order that opened or last added to it).
        // Cleared when the position closes, so old orders can never trigger exits on a new position.
        struct ExitLevels { double stopLoss = 0; double takeProfit = 0; };
        std::map<std::string, ExitLevels> exitLevels;
        void updateExitLevels(const Order& order);
        void exitPosition(const std::string& ticker, double quantity, const char* reason, double level);
        SlippageMode slippageMode = SlippageMode::Random;
        bool useLatestBar = false;

        // Step-by-step notes (set-up, each step, position changes): only with enableDetailedLogging.
        // Fills, and stop loss / take profit hits, always go to Log::out().
        std::ostream& bookkeeping() const { return detailedLogging ? Log::out() : Log::silent(); }
        bool severalStocks = false;
        std::map<std::string, float> latestPrices;   // each stock's last close (several stocks in one simulation)

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
