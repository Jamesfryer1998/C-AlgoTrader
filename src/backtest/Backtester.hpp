#pragma once

#include <iostream>
#include <vector>
#include <unistd.h>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <map>
#include "../data_access/MarketData.hpp"
#include "../strategy_engine/StrategyEngine.hpp"
#include "../strategy_engine/StrategyFactory.hpp"
#include "../broker/SimulatedBroker.hpp"
#include "BacktestMarketDataAdapter.hpp"

/**
 * Performance metrics for backtesting results
 */
struct PerformanceMetrics {
    double startingCapital;
    double finalEquity;
    double totalPnL;
    double totalPnLPercent;
    double maxDrawdownPercent;
    double sharpeRatio;
    int numTrades;
    int winningTrades;
    int losingTrades;
    double winRate;
    double avgWin;
    double avgLoss;
    double profitFactor;
    double annualizedReturn;
    std::chrono::duration<double> executionTime;
};

/**
 * Backtester class
 * 
 * Manages the backtesting of trading strategies with historical market data.
 * Uses adapter patterns to coordinate interactions between core components without
 * modifying their implementation.
 */
class Backtester {
public:
    /**
     * Constructor
     * @param algoConfig JSON configuration for the backtester
     */
    Backtester(const json& algoConfig);

    /**
     * Run the backtest simulation
     */
    void run();
    
    // Configuration methods
    void setStartingCapital(double capital);
    void setCommissionPerTrade(double commission);
    void setSlippagePercentage(double slippage);
    void setDataTimeframe(const std::string& timeframe);
    void enableDetailedLogging(bool enable);
    void saveResultsToFile(const std::string& filename);
    void setDateRange(const std::string& startDate, const std::string& endDate);
    void setNumThreads(int threads);
    
    // Testing support
    void setMarketData(std::vector<MarketCondition>& mockData);
    void useDirectMarketData(bool useDirect);
    
    // Result access methods
    const PerformanceMetrics& getPerformanceMetrics() const;
    
private:
    // Core components
    MarketData marketData;                    // Raw market data instance
    BacktestMarketDataAdapter marketDataAdapter;  // Adapter for backtest control
    SimulatedBroker broker;                   // Simulated broker for order execution
    StrategyFactory stratFactory;             // Factory for creating strategies
    StrategyEngine stratEngine;               // Engine for running strategies
    
    // Configuration
    json algoConfig;
    bool detailedLogging;
    std::string resultsFilename;
    std::string startDate;
    std::string endDate;
    int numThreads;
    bool useDirectData;
    
    // Performance tracking
    PerformanceMetrics metrics;
    std::vector<double> dailyReturns;
    std::vector<std::pair<std::string, double>> equityCurve;
    std::map<std::string, std::vector<double>> customMetrics;
    
    // Simulation methods
    void initializeBacktest();
    void executeTimeStep();
    void logPerformance();
    void calculateMetrics();
    void printReport();
    void saveResults();
    
    // Calculation helpers
    double calculateSharpeRatio();
    double calculateMaxDrawdown();
    double calculateAnnualizedReturn();
    
    // Utility methods
    std::string getCurrentTimestamp() const;
    std::string formatCurrency(double amount) const;
    std::string formatPercent(double percentage) const;
};