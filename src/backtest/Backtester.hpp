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
#include "BacktestCosts.hpp"
#include "BacktestMarketDataAdapter.hpp"
#include "../oms/PerformanceTracker.hpp"

/**
 * Performance metrics for backtesting results
 */
struct PerformanceMetrics {
    double startingCapital;
    double finalEquity;
    double totalPnL;
    double totalPnLPercent;
    double realisedPnL;
    double unrealisedPnL;
    double commissionPaid;
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

    /**
     * Run the backtest over bars you already have in memory (oldest first). The bars are only read,
     * never copied, so many backtests can share one dataset (the optimiser does this).
     * Strategies see a rolling window of the newest bars, exactly like live trading.
     */
    void runOn(const std::vector<MarketCondition>& bars);

    // Bars kept in memory for strategies (default 1000; strategies only look back a fixed amount)
    void setWindowBars(size_t bars) { windowBars = bars; }

    // The first `bars` bars passed to runOn only fill the strategies' history: no decisions, no trades,
    // not counted in the results. Lets a test period start with strategies already warmed up.
    void setWarmupBars(size_t bars) { warmupBars = bars; }

    // Keep a timestamped equity point per bar (only needed for --output files). Off saves memory.
    void keepEquityCurve(bool keep) { keepCurve = keep; }

    // P&L, Sharpe, drawdown etc. from the last run
    PerformanceSummary getSummary() const { return tracker.summary(); }

    // (bar time, account equity) after each bar of the last run, when keepEquityCurve is on.
    // The first point is the starting equity, stamped with the first bar's time.
    const std::vector<std::pair<std::string, double>>& getEquityCurve() const { return equityCurve; }
    SimulatedBroker& getBroker() { return broker; }
    
    // Configuration methods
    void setStartingCapital(double capital);
    void setCommissionPerTrade(double commission);
    void setSlippagePercentage(double slippage);
    // Capital, commission and slippage together (slippage always against us, as in live trading)
    void useCosts(const BacktestCosts& costs);
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
    size_t windowBars = 1000;
    size_t warmupBars = 0;
    double periodsPerYear;                    // bars in a year, for Sharpe: config "bars_per_year" (default 1-minute bars)
    bool keepCurve = true;
    
    // Performance tracking
    PerformanceMetrics metrics;
    PerformanceTracker tracker;               // works out P&L, Sharpe, drawdown from the broker's fills
    size_t fillsRecorded = 0;                 // how many of the broker's fills the tracker has seen
    std::vector<std::pair<std::string, double>> equityCurve;
    std::map<std::string, std::vector<double>> customMetrics;
    
    // Simulation methods
    void initializeBacktest();
    void executeTimeStep(const MarketCondition& bar);
    void logPerformance();
    void calculateMetrics();
    void printReport();
    void saveResults();
    
    void recordNewFills();
    
    // Utility methods
    std::string getCurrentTimestamp() const;
    std::string formatCurrency(double amount) const;
    std::string formatPercent(double percentage) const;
};