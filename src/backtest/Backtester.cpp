#include "Backtester.hpp"
#include <cmath>
#include <numeric>
#include <iomanip>

Backtester::Backtester(const json& algoConfig)
        : marketData(), 
          broker(marketData),
          stratFactory(algoConfig), 
          stratEngine(),
          algoConfig(algoConfig),
          detailedLogging(false),
          resultsFilename(""),
          numThreads(0), // 0 means use all available cores
          useDirectData(false)
{
    // Initialize the performance metrics
    metrics = PerformanceMetrics();
    
    // Initialize the market data adapter
    marketDataAdapter.initialize(marketData);
    
    // Configure the broker with default settings
    broker.setStartingCapital(100000.0);
    broker.setCommission(1.0);
    broker.setSlippage(0.0005);

    // Default date range (last 7 days)
    auto now = std::chrono::system_clock::now();
    auto endTimeT = std::chrono::system_clock::to_time_t(now);
    auto startTimeT = endTimeT - (7 * 24 * 60 * 60); // 7 days earlier
    
    std::stringstream ssEnd, ssStart;
    ssEnd << std::put_time(std::localtime(&endTimeT), "%Y-%m-%d");
    ssStart << std::put_time(std::localtime(&startTimeT), "%Y-%m-%d");
    
    endDate = ssEnd.str();
    startDate = ssStart.str();
    
    std::cout << "Default date range: " << startDate << " to " << endDate << std::endl;
}

void Backtester::setStartingCapital(double capital) {
    broker.setStartingCapital(capital);
}

void Backtester::setCommissionPerTrade(double commission) {
    broker.setCommission(commission);
}

void Backtester::setSlippagePercentage(double slippage) {
    broker.setSlippage(slippage);
    std::cout << "Backtester: Setting slippage to " << std::fixed << std::setprecision(3) 
              << (slippage * 100.0) << "% (random variation within +/- this range)" << std::endl;
}

void Backtester::enableDetailedLogging(bool enable) {
    detailedLogging = enable;
}

void Backtester::saveResultsToFile(const std::string& filename) {
    resultsFilename = filename;
}

void Backtester::setDateRange(const std::string& start, const std::string& end) {
    startDate = start;
    endDate = end;
    std::cout << "Set backtest date range: " << startDate << " to " << endDate << std::endl;
}

void Backtester::setNumThreads(int threads) {
    numThreads = threads;
}

void Backtester::setMarketData(std::vector<MarketCondition>& mockData) {
    std::cout << "Setting mock market data with " << mockData.size() << " data points" << std::endl;
    
    // Verify the mock data is not empty
    if (mockData.empty()) {
        std::cerr << "ERROR: Attempted to set empty mock data in Backtester::setMarketData!" << std::endl;
        return;
    }
    
    // Load the mock data into the adapter
    marketDataAdapter.loadMockData(mockData);
    
    // Auto-enable direct data mode when mock data is provided
    useDirectData = true;
    
    std::cout << "Verified market data size: " << marketDataAdapter.getDataSize() << " data points" << std::endl;
    if (marketDataAdapter.getDataSize() > 0) {
        MarketCondition firstPoint = marketDataAdapter.getCurrentData();
        std::cout << "First data point: " << firstPoint.DateTime << std::endl;
    }
    std::cout << "Current index: " << marketDataAdapter.getCurrentIndex() << std::endl;
}

void Backtester::useDirectMarketData(bool useDirect) {
    useDirectData = useDirect;
}

void 
Backtester::run() 
{
    std::cout << "Starting backtest from " << startDate << " to " << endDate << "..." << std::endl;
    
    // Record start time
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Only process market data if not using direct data mode
    if (!useDirectData) {
        // Process market data with date range and parallel processing
        if (numThreads > 0) {
            std::cout << "Using " << numThreads << " threads for processing" << std::endl;
            marketDataAdapter.loadHistoricalDataParallel(algoConfig, numThreads, startDate, endDate);
        } else {
            std::cout << "Using all available cores for processing" << std::endl;
            marketDataAdapter.loadHistoricalData(algoConfig, startDate, endDate);
        }
    } else {
        std::cout << "Using directly provided market data (" << marketDataAdapter.getDataSize() << " data points)" << std::endl;
    }
    
    // Verify we have data before proceeding
    if (marketDataAdapter.getDataSize() == 0) {
        std::cerr << "ERROR: No market data available for backtesting. Exiting run()." << std::endl;
        return;
    }
    
    // Ensure we're at the beginning of the data
    marketDataAdapter.rewind();
    
    // Set up strategy engine
    stratEngine.setUp(algoConfig, stratFactory, marketData, &broker);
    
    // Initialize backtest
    initializeBacktest();
    
    // Log initial state - ensure we have data before trying to access it
    if (marketDataAdapter.getDataSize() > 0) {
        MarketCondition firstPoint = marketDataAdapter.getCurrentData();
        equityCurve.push_back({firstPoint.DateTime, broker.getCurrentEquity()});
    } else {
        std::cerr << "ERROR: Market data is empty after initialization. Exiting run()." << std::endl;
        return;
    }
    
    // Count how many data points we'll process
    size_t totalDataPoints = marketDataAdapter.getDataSize();
    std::cout << "Processing " << totalDataPoints << " market data points" << std::endl;
    
    // Progress tracking
    size_t lastProgress = 0;
    size_t progressStep = totalDataPoints / 20; // Show progress in 5% increments
    if (progressStep == 0) progressStep = 1;
        
    // Add explicit counter to detect infinite loops
    int loopCount = 0;
    const int MAX_LOOPS = totalDataPoints * 2; // Safety limit
        
    while (marketDataAdapter.hasNext() && loopCount < MAX_LOOPS) {         
        executeTimeStep();
        loopCount++;
        
        // Show progress
        size_t currentIndex = marketDataAdapter.getCurrentIndex();
        if (currentIndex - lastProgress >= progressStep) {
            int progressPercent = static_cast<int>((static_cast<double>(currentIndex) / totalDataPoints) * 100);
            std::cout << "Progress: " << progressPercent << "% ("
                      << currentIndex << "/" << totalDataPoints << " data points)"
                      << std::endl;
            lastProgress = currentIndex;
        }
    }
    
    // Check if we exited due to the safety limit
    if (loopCount >= MAX_LOOPS) {
        std::cerr << "WARNING: Loop safety limit reached. Possible infinite loop detected." << std::endl;
    }
        
    // Record end time and calculate duration
    auto endTime = std::chrono::high_resolution_clock::now();
    metrics.executionTime = endTime - startTime;
    
    // Calculate final metrics
    calculateMetrics();
    
    // Print report
    printReport();
    
    // Save results to file if requested
    if (!resultsFilename.empty()) {
        saveResults();
    }
    
    std::cout << "Backtest completed in " 
              << metrics.executionTime.count() << " seconds" << std::endl;
}

void
Backtester::initializeBacktest() 
{
    // Reset performance tracking
    equityCurve.clear();
    customMetrics.clear();
    tracker = PerformanceTracker(broker.getStartingCapital(), PerformanceTracker::MINUTE_BARS_PER_YEAR);
    fillsRecorded = 0;
    
    // Log initial state
    metrics.startingCapital = broker.getStartingCapital();
}

void
Backtester::executeTimeStep() 
{
    // 1. Process next data point using the adapter
    // This updates the underlying MarketData with the next time point
    marketDataAdapter.next();
    
    // Get the current data point for logging and synchronization
    MarketCondition currentData = marketDataAdapter.getCurrentData();
    std::string timestamp = currentData.DateTime;
    
    // Log the timestamp we're about to process
    std::cout << "Backtester time step: " << timestamp << std::endl;
    
    // 2. Execute strategy - this will use the updated marketData
    // The strategy will generate signals based on this data point
    stratEngine.run();
    
    // 3. Simulate broker operations - this includes processing orders from strategies
    // The broker will use the same current data point for execution
    broker.nextStep();
    
    // 4. Log performance and update metrics
    logPerformance();
    
    // Add to equity curve using current timestamp
    equityCurve.push_back({timestamp, broker.getCurrentEquity()});

    // Feed this bar's fills and closing price to the performance tracker
    recordNewFills();
    tracker.markPrice(currentData.Ticker, currentData.Close);
    tracker.recordEquity();
}

void
Backtester::recordNewFills()
{
    const std::vector<Order>& fills = broker.getFilledOrders();
    for (; fillsRecorded < fills.size(); fillsRecorded++) {
        const Order& fill = fills[fillsRecorded];
        tracker.recordFill(fill.getTicker(), fill.isBuy(), fill.getQuantity(), fill.getPrice(),
                           broker.getCommissionPerTrade());
    }
}

void 
Backtester::logPerformance() 
{
    if (detailedLogging) {
        std::cout << "Current Equity: " << formatCurrency(broker.getCurrentEquity())
                  << " | PnL: " << formatCurrency(broker.getPnL())
                  << " | Drawdown: " << formatPercent(broker.getDrawdown())
                  << "%" << std::endl;
    }
}

void
Backtester::calculateMetrics() 
{
    PerformanceSummary summary = tracker.summary();

    metrics.finalEquity = broker.getCurrentEquity();
    metrics.totalPnL = broker.getPnL();
    metrics.totalPnLPercent = (metrics.finalEquity - metrics.startingCapital) / metrics.startingCapital * 100.0;
    metrics.numTrades = broker.getNumTrades();

    metrics.realisedPnL = summary.realisedPnL;
    metrics.unrealisedPnL = summary.unrealisedPnL;
    metrics.commissionPaid = summary.commissionPaid;
    metrics.winningTrades = summary.winningTrades;
    metrics.losingTrades = summary.losingTrades;
    metrics.winRate = summary.winRatePercent;
    metrics.avgWin = summary.avgWin;
    metrics.avgLoss = summary.avgLoss;
    metrics.profitFactor = summary.profitFactor;
    metrics.sharpeRatio = summary.sharpeRatio;
    metrics.maxDrawdownPercent = summary.maxDrawdownPercent;
    metrics.annualizedReturn = summary.annualisedReturnPercent;

    // The tracker and the broker keep separate books; they should always agree on equity
    if (std::fabs(summary.equity - metrics.finalEquity) > 0.01)
        std::cerr << "WARNING: performance tracker equity " << summary.equity
                  << " differs from broker equity " << metrics.finalEquity << std::endl;
}

void 
Backtester::printReport() 
{
    std::cout << "\n===== BACKTEST RESULTS =====\n" << std::endl;
    
    // General information
    std::cout << "Starting capital: " << formatCurrency(metrics.startingCapital) << std::endl;
    std::cout << "Final equity: " << formatCurrency(metrics.finalEquity) << std::endl;
    std::cout << "Total P&L: " << formatCurrency(metrics.totalPnL) 
              << " (" << formatPercent(metrics.totalPnLPercent) << "%)" << std::endl;
    std::cout << "  - Realised: " << formatCurrency(metrics.realisedPnL)
              << " | Unrealised: " << formatCurrency(metrics.unrealisedPnL)
              << " | Commission: " << formatCurrency(-metrics.commissionPaid) << std::endl;
    std::cout << "Execution time: " << metrics.executionTime.count() << " seconds" << std::endl;
    
    // Performance metrics
    std::cout << "\nPerformance metrics:" << std::endl;
    std::cout << "- Sharpe ratio: " << std::fixed << std::setprecision(2) << metrics.sharpeRatio << std::endl;
    std::cout << "- Max drawdown: " << formatPercent(metrics.maxDrawdownPercent) << "%" << std::endl;
    std::cout << "- Annualized return: " << formatPercent(metrics.annualizedReturn) << "%" << std::endl;
    
    // Trade statistics
    std::cout << "\nTrade statistics:" << std::endl;
    std::cout << "- Total fills: " << metrics.numTrades << std::endl;
    std::cout << "- Closed trades (fills that closed or reduced a position):" << std::endl;
    std::cout << "- Winning trades: " << metrics.winningTrades 
              << " (" << formatPercent(metrics.winRate) << "%)" << std::endl;
    std::cout << "- Losing trades: " << metrics.losingTrades << std::endl;
    std::cout << "- Average win: " << formatCurrency(metrics.avgWin) << std::endl;
    std::cout << "- Average loss: " << formatCurrency(metrics.avgLoss) << std::endl;
    std::cout << "- Profit factor: " << std::fixed << std::setprecision(2) << metrics.profitFactor << std::endl;
    
    std::cout << "\n============================\n" << std::endl;
}

void 
Backtester::saveResults() 
{
    std::ofstream outFile(resultsFilename);
    
    if (!outFile) {
        std::cerr << "Error: Unable to open file " << resultsFilename << " for writing." << std::endl;
        return;
    }
    
    // Write header
    outFile << "Backtest Results - " << getCurrentTimestamp() << "\n\n";
    
    // General information
    outFile << "Starting capital: " << formatCurrency(metrics.startingCapital) << "\n";
    outFile << "Final equity: " << formatCurrency(metrics.finalEquity) << "\n";
    outFile << "Total P&L: " << formatCurrency(metrics.totalPnL) 
            << " (" << formatPercent(metrics.totalPnLPercent) << "%)\n";
    outFile << "Realised P&L: " << formatCurrency(metrics.realisedPnL) << "\n";
    outFile << "Unrealised P&L: " << formatCurrency(metrics.unrealisedPnL) << "\n";
    outFile << "Commission: " << formatCurrency(metrics.commissionPaid) << "\n";
    outFile << "Execution time: " << metrics.executionTime.count() << " seconds\n\n";
    
    // Performance metrics
    outFile << "Performance metrics:\n";
    outFile << "- Sharpe ratio: " << std::fixed << std::setprecision(2) << metrics.sharpeRatio << "\n";
    outFile << "- Max drawdown: " << formatPercent(metrics.maxDrawdownPercent) << "%\n";
    outFile << "- Annualized return: " << formatPercent(metrics.annualizedReturn) << "%\n\n";
    
    // Trade statistics
    outFile << "Trade statistics:\n";
    outFile << "- Total trades: " << metrics.numTrades << "\n";
    outFile << "- Winning trades: " << metrics.winningTrades 
            << " (" << formatPercent(metrics.winRate) << "%)\n";
    outFile << "- Losing trades: " << metrics.losingTrades << "\n";
    outFile << "- Average win: " << formatCurrency(metrics.avgWin) << "\n";
    outFile << "- Average loss: " << formatCurrency(metrics.avgLoss) << "\n";
    outFile << "- Profit factor: " << std::fixed << std::setprecision(2) << metrics.profitFactor << "\n\n";
    
    // Write equity curve
    outFile << "Equity Curve:\n";
    outFile << "Timestamp,Equity\n";
    
    for (const auto& point : equityCurve) {
        outFile << point.first << "," << point.second << "\n";
    }
    
    outFile.close();
    
    std::cout << "Results saved to " << resultsFilename << std::endl;
}

const PerformanceMetrics& 
Backtester::getPerformanceMetrics() const 
{
    return metrics;
}

std::string 
Backtester::getCurrentTimestamp() const 
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string 
Backtester::formatCurrency(double amount) const 
{
    std::stringstream ss;
    ss << "$" << std::fixed << std::setprecision(2) << amount;
    return ss.str();
}

std::string 
Backtester::formatPercent(double percentage) const 
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << percentage;
    return ss.str();
}