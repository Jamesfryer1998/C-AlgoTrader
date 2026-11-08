#include "StrategyTester.hpp"
#include "../util/Log.hpp"
#include "../util/MarketTime.hpp"
#include <cmath>
#include <numeric>
#include <iomanip>
#include "../risk/RiskEngineFactory.hpp"
#include "../util/Defaults.hpp"
#include "../util/SaveData.hpp"

namespace
{
    const double PERCENT = 100.0;
    const double CENT = 0.01;                  // P&L figures closer than this agree
    const char* const TIMESTAMP_FORMAT = "%Y-%m-%d %H:%M:%S";
}

StrategyTester::StrategyTester(const json& algoConfig)
        : marketData(), 
          broker(marketData),
          stratFactory(algoConfig), 
          stratEngine(),
          algoConfig(algoConfig),
          detailedLogging(false),
          resultsFilename(""),
          numThreads(0), // 0 means use all available cores
          useDirectData(false),
          periodsPerYear(algoConfig.value("bars_per_year", PerformanceTracker::MINUTE_BARS_PER_YEAR))
{
    // Initialize the performance metrics
    metrics = PerformanceMetrics();
    
    // Initialize the market data adapter
    marketDataAdapter.initialize(marketData);
    
    // Configure the broker with default settings
    broker.setStartingCapital(Defaults::STARTING_CAPITAL);
    broker.setCommission(Defaults::COMMISSION_PER_TRADE);
    broker.setSlippage(Defaults::SLIPPAGE);
    broker.followLatestBar(true);   // the broker trades on the newest bar of the rolling window

    // Default date range: the last few days (thread-safe; many Backtesters may be created at once)
    std::time_t now = std::time(nullptr);
    endDate = MarketTime::datePart(MarketTime::toNewYorkTime(now));
    startDate = MarketTime::datePart(MarketTime::toNewYorkTime(now - Defaults::BACKTEST_DAYS * MarketTime::SECONDS_PER_DAY));
    
    Log::out() << "Default date range: " << startDate << " to " << endDate << std::endl;
}

void StrategyTester::setStartingCapital(double capital) {
    broker.setStartingCapital(capital);
}

void StrategyTester::setCommissionPerTrade(double commission) {
    broker.setCommission(commission);
}

void StrategyTester::setSlippagePercentage(double slippage) {
    broker.setSlippage(slippage);
    Log::out() << "StrategyTester: Setting slippage to " << std::fixed << std::setprecision(3) 
              << (slippage * 100.0) << "% (random variation within +/- this range)" << std::endl;
}

void StrategyTester::useCosts(const TradingCosts& costs) {
    setStartingCapital(costs.startingCapital);
    setCommissionPerTrade(costs.commissionPerTrade);
    setSlippagePercentage(costs.slippageFraction());
    broker.setRegulatoryFees(costs.regulatoryFees);
    broker.setSlippageMode(SimulatedBroker::SlippageMode::Adverse);
}

void StrategyTester::enableDetailedLogging(bool enable) {
    detailedLogging = enable;
}

void StrategyTester::saveResultsToFile(const std::string& filename) {
    resultsFilename = filename;
}

void StrategyTester::setDateRange(const std::string& start, const std::string& end) {
    startDate = start;
    endDate = end;
    Log::out() << "Set backtest date range: " << startDate << " to " << endDate << std::endl;
}

void StrategyTester::setNumThreads(int threads) {
    numThreads = threads;
}

void StrategyTester::setMarketData(std::vector<MarketCondition>& mockData) {
    Log::out() << "Setting mock market data with " << mockData.size() << " data points" << std::endl;
    
    // Verify the mock data is not empty
    if (mockData.empty()) {
        std::cerr << "ERROR: Attempted to set empty mock data in StrategyTester::setMarketData!" << std::endl;
        return;
    }
    
    // Load the mock data into the adapter
    marketDataAdapter.loadMockData(mockData);
    
    // Auto-enable direct data mode when mock data is provided
    useDirectData = true;
    
    Log::out() << "Verified market data size: " << marketDataAdapter.getDataSize() << " data points" << std::endl;
    if (marketDataAdapter.getDataSize() > 0) {
        MarketCondition firstPoint = marketDataAdapter.getCurrentData();
        Log::out() << "First data point: " << firstPoint.DateTime << std::endl;
    }
    Log::out() << "Current index: " << marketDataAdapter.getCurrentIndex() << std::endl;
}

void StrategyTester::useDirectMarketData(bool useDirect) {
    useDirectData = useDirect;
}

void 
StrategyTester::run() 
{
    std::cout << "Starting backtest from " << startDate << " to " << endDate << "..." << std::endl;
    
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
    
    runOn(marketDataAdapter.getFullDataset());
}

void
StrategyTester::runOn(const std::vector<MarketCondition>& bars)
{
    auto startTime = std::chrono::high_resolution_clock::now();
    if (bars.size() <= warmupBars) {
        std::cerr << "ERROR: No market data available for backtesting." << std::endl;
        return;
    }

    // Start from an empty window; bars are added one at a time below
    std::vector<MarketCondition> none;
    marketData.update(none);

    stratEngine.setUp(algoConfig, stratFactory, marketData);
    oms = OrderManagement();
    oms.setUp(algoConfig, &broker);
    oms.setMarketData(marketData);
    risk = RiskEngineFactory::create(algoConfig);
    initializeBacktest();

    // Warm-up bars only build the strategies' history
    for (size_t i = 0; i < warmupBars; i++)
        marketData.addBar(bars[i], windowBars);
    equityCurve.push_back({bars[warmupBars].DateTime, broker.getCurrentEquity()});

    Log::out() << "Processing " << bars.size() - warmupBars << " market data points" << std::endl;
    size_t progressStep = std::max<size_t>(1, bars.size() / 20);   // progress every 5%

    for (size_t i = warmupBars; i < bars.size(); i++) {
        executeTimeStep(bars[i]);

        if ((i + 1) % progressStep == 0)
            Log::out() << "Progress: " << (100 * (i + 1) / bars.size()) << "% ("
                       << (i + 1) << "/" << bars.size() << " data points)" << std::endl;
    }

    metrics.executionTime = std::chrono::high_resolution_clock::now() - startTime;
    calculateMetrics();

    if (Log::verbose)
        printReport();

    if (!resultsFilename.empty())
        saveResults();

    Log::out() << "Backtest completed in " << metrics.executionTime.count() << " seconds" << std::endl;
}

void
StrategyTester::initializeBacktest() 
{
    // Reset performance tracking
    equityCurve.clear();
    customMetrics.clear();
    tracker = PerformanceTracker(broker.getStartingCapital(), periodsPerYear);
    fillsRecorded = 0;
    
    // Log initial state
    metrics.startingCapital = broker.getStartingCapital();
}

void
StrategyTester::executeTimeStep(const MarketCondition& bar)
{
    // 1. The new bar arrives (strategies see the newest windowBars bars, like live trading)
    marketData.addBar(bar, windowBars);
    Log::out() << "StrategyTester time step: " << bar.DateTime << std::endl;

    // 2. Strategies vote; the risk engine sizes an agreed order, which goes through the risk checks to the
    //    simulated broker
    float held = oms.heldQuantity(bar.Ticker);
    std::optional<Order> order = stratEngine.decide(held);
    if (order)
    {
        RiskEngine::SizingContext context;
        context.equity = broker.getCurrentEquity();
        context.heldQuantity = held;
        context.bars = &marketData;
        context.closedTradeReturns = &tracker.closedTradeReturns(bar.Ticker);
        order = risk->size(*order, context);
    }
    if (order)
        oms.sendOrder(*order);

    // 3. The broker fills orders and checks stop losses / take profits on this bar,
    //    then the OMS catches up with the filled position (the live app does the same after each bar)
    broker.nextStep();
    oms.syncPosition(bar.Ticker);
    logPerformance();

    // 4. Book-keeping
    if (keepCurve)
        equityCurve.push_back({bar.DateTime, broker.getCurrentEquity()});
    recordNewFills();
    tracker.markPrice(bar.Ticker, bar.Close);
    tracker.recordEquity();
}

void
StrategyTester::recordNewFills()
{
    const std::vector<Order>& fills = broker.getFilledOrders();
    const std::vector<double>& fees = broker.getFillFees();
    for (; fillsRecorded < fills.size(); fillsRecorded++) {
        const Order& fill = fills[fillsRecorded];
        tracker.recordFill(fill.getTicker(), fill.isBuy(), fill.getQuantity(), fill.getPrice(), fees[fillsRecorded]);
    }
}

void 
StrategyTester::logPerformance() 
{
    if (detailedLogging) {
        std::cout << "Current Equity: " << formatCurrency(broker.getCurrentEquity())
                  << " | PnL: " << formatCurrency(broker.getPnL())
                  << " | Drawdown: " << formatPercent(broker.getDrawdown())
                  << "%" << std::endl;
    }
}

void
StrategyTester::calculateMetrics() 
{
    PerformanceSummary summary = tracker.summary();

    metrics.finalEquity = broker.getCurrentEquity();
    metrics.totalPnL = broker.getPnL();
    metrics.totalPnLPercent = (metrics.finalEquity - metrics.startingCapital) / metrics.startingCapital * PERCENT;
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
    if (std::fabs(summary.equity - metrics.finalEquity) > CENT)
        std::cerr << "WARNING: performance tracker equity " << summary.equity
                  << " differs from broker equity " << metrics.finalEquity << std::endl;
}

void 
StrategyTester::printReport() 
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
StrategyTester::saveResults() 
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
    SaveData::saved(resultsFilename);
    
    std::cout << "Results saved to " << resultsFilename << std::endl;
}

const PerformanceMetrics& 
StrategyTester::getPerformanceMetrics() const 
{
    return metrics;
}

std::string 
StrategyTester::getCurrentTimestamp() const 
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), TIMESTAMP_FORMAT);
    return ss.str();
}

std::string 
StrategyTester::formatCurrency(double amount) const 
{
    std::stringstream ss;
    ss << "$" << std::fixed << std::setprecision(2) << amount;
    return ss.str();
}

std::string 
StrategyTester::formatPercent(double percentage) const 
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << percentage;
    return ss.str();
}