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

class Backtester {
public:
    Backtester(const json& algoConfig);

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
    
    // Result access methods
    const PerformanceMetrics& getPerformanceMetrics() const;
    
private:
    // Core components
    MarketData marketData;
    SimulatedBroker broker;
    StrategyFactory stratFactory;
    StrategyEngine stratEngine;
    
    // Configuration
    json algoConfig;
    bool detailedLogging;
    std::string resultsFilename;
    std::string startDate;
    std::string endDate;
    int numThreads;
    
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