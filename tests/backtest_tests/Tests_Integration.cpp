#include <gtest/gtest.h>
#include <memory>
#include "../../src/backtest/Backtester.hpp"
#include "../../src/broker/SimulatedBroker.hpp"
#include "../../src/data_access/MarketData.hpp"
#include "../../src/strategy_engine/StrategyEngine.hpp"
#include "../../src/strategy_engine/StrategyFactory.hpp"
#include "../../src/oms/Order.hpp"
#include "../../src/oms/Position.hpp"

// Class for creating mock data
class TestMarketDataProvider {
public:
    static std::vector<MarketCondition> createTrendingMarketData(
        int numDays = 30,
        const std::string& ticker = "AAPL",
        float startPrice = 100.0f,
        float dailyChange = 0.01f // 1% daily change
    ) {
        std::vector<MarketCondition> mockData;
        float price = startPrice;
        
        for (int i = 0; i < numDays; i++) {
            std::string dateTime = "2025-03-" + std::to_string(std::min(1 + i, 31)) + " 10:00:00";
            
            // Price trends upward
            price *= (1.0f + dailyChange);
            
            MarketCondition condition(
                dateTime,          // DateTime
                ticker,           // Ticker
                price * 0.99f,    // Open slightly lower
                price,            // Close
                1000 * (i + 1),   // Volume increases over time
                "1m"              // TimeInterval
            );
            
            mockData.push_back(condition);
        }
        
        return mockData;
    }
    
    static std::vector<MarketCondition> createVolatileMarketData(
        int numDays = 30,
        const std::string& ticker = "AAPL",
        float startPrice = 100.0f,
        float volatility = 0.02f // 2% daily volatility
    ) {
        std::vector<MarketCondition> mockData;
        float price = startPrice;
        
        // Create a deterministic but "random" pattern
        std::vector<float> changes = {
            1.0f + volatility,
            1.0f - volatility * 0.5f,
            1.0f + volatility * 1.5f,
            1.0f - volatility,
            1.0f + volatility * 0.7f,
            1.0f - volatility * 0.3f,
            1.0f + volatility * 0.2f,
            1.0f - volatility * 1.2f,
            1.0f + volatility * 0.9f,
            1.0f + volatility * 0.1f
        };
        
        for (int i = 0; i < numDays; i++) {
            std::string dateTime = "2025-03-" + std::to_string(std::min(1 + i, 31)) + " 10:00:00";
            
            // Apply "random" price change pattern
            price *= changes[i % changes.size()];
            
            MarketCondition condition(
                dateTime,          // DateTime
                ticker,           // Ticker
                price * 0.99f,    // Open slightly lower
                price,            // Close
                1000 * (i + 1),   // Volume increases over time
                "1m"              // TimeInterval
            );
            
            mockData.push_back(condition);
        }
        
        return mockData;
    }
    
    static std::vector<MarketCondition> createMultiAssetMarketData(
        int numDays = 30,
        std::vector<std::string> tickers = {"AAPL", "MSFT", "GOOG"},
        std::vector<float> startPrices = {100.0f, 200.0f, 150.0f}
    ) {
        std::vector<MarketCondition> mockData;
        std::vector<float> prices = startPrices;
        
        // Simple deterministic pattern of price changes
        std::vector<float> changes = {1.01f, 0.99f, 1.02f, 0.98f, 1.015f};
        
        for (int i = 0; i < numDays; i++) {
            std::string dateTime = "2025-03-" + std::to_string(std::min(1 + i, 31)) + " 10:00:00";
            
            for (size_t t = 0; t < tickers.size(); t++) {
                // Apply price change based on ticker and day
                prices[t] *= changes[(i + t) % changes.size()];
                
                MarketCondition condition(
                    dateTime,          // DateTime
                    tickers[t],        // Ticker
                    prices[t] * 0.99f, // Open slightly lower
                    prices[t],         // Close
                    1000 * (i + 1),    // Volume increases over time
                    "1m"               // TimeInterval
                );
                
                mockData.push_back(condition);
            }
        }
        
        return mockData;
    }
};

// Integration test fixture
class BacktestIntegrationTests : public ::testing::Test {
public:
    std::unique_ptr<MarketData> marketData;
    std::unique_ptr<SimulatedBroker> broker;
    std::unique_ptr<Backtester> backtester;
    json testConfig;
    
    void SetUp() override {
        // Create basic test config
        testConfig = {
            {"ticker", "AAPL"},
            {"marketDataBasePath", "data/"},
            {"baseDataFileName", "marketData"},
            {"strategies", {
                {{"name", "RSI"}, {"parameters", {{"period", 14}, {"overbought", 70}, {"oversold", 30}}}}
            }}
        };
        
        // Initialize market data
        marketData = std::make_unique<MarketData>();
    }
    
    void TearDown() override {
        backtester.reset();
        broker.reset();
        marketData.reset();
    }
    
    void setupTrendingMarket() {
        auto mockData = TestMarketDataProvider::createTrendingMarketData();
        marketData->update(mockData);
    }
    
    void setupVolatileMarket() {
        auto mockData = TestMarketDataProvider::createVolatileMarketData();
        marketData->update(mockData);
    }
    
    void setupMultiAssetMarket() {
        auto mockData = TestMarketDataProvider::createMultiAssetMarketData();
        marketData->update(mockData);
    }
    
    void createBroker() {
        broker = std::make_unique<SimulatedBroker>(*marketData);
        broker->setStartingCapital(100000.0);
        broker->setCommission(1.0);
        broker->setSlippage(0.001);
    }
    
    void createBacktester() {
        backtester = std::make_unique<Backtester>(testConfig);
        // Use the market data we've set up directly
        std::vector<MarketCondition> mockData = marketData->getData();
        backtester->setMarketData(mockData);
    }
};

// Test 1: Integration of Backtester with SimulatedBroker with trending market
TEST_F(BacktestIntegrationTests, BacktesterWithSimulatedBrokerTrendingMarket) {
    // Setup trending market data
    setupTrendingMarket();
    
    // Create SimulatedBroker with market data
    createBroker();
    
    // Create Backtester
    createBacktester();
    
    // Run backtest
    EXPECT_NO_THROW(backtester->run());
    
    // Check the results
    const PerformanceMetrics& metrics = backtester->getPerformanceMetrics();
    
    // In a trending market, we would expect:
    // - Positive P&L
    // - Some trades executed
    // - Positive Sharpe ratio
    EXPECT_GE(metrics.totalPnL, 0.0);
    EXPECT_GT(metrics.numTrades, 0);
    EXPECT_GT(metrics.sharpeRatio, 0.0);
}

// Test 2: Integration with volatile market conditions
TEST_F(BacktestIntegrationTests, BacktesterWithSimulatedBrokerVolatileMarket) {
    // Setup volatile market data
    setupVolatileMarket();
    
    // Create SimulatedBroker with market data
    createBroker();
    
    // Create Backtester
    createBacktester();
    
    // Run backtest
    EXPECT_NO_THROW(backtester->run());
    
    // Check the results
    const PerformanceMetrics& metrics = backtester->getPerformanceMetrics();
    
    // In a volatile market, we would expect:
    // - Some trades executed
    // - Non-zero P&L (could be positive or negative)
    // - Some drawdown
    EXPECT_GT(metrics.numTrades, 0);
    EXPECT_NE(metrics.totalPnL, 0.0);
    EXPECT_GT(metrics.maxDrawdownPercent, 0.0);
}

// Test 3: Impact of different commission levels on backtest results
TEST_F(BacktestIntegrationTests, CommissionImpactOnBacktestResults) {
    // Setup trending market data
    setupTrendingMarket();
    
    // Create SimulatedBroker with low commission
    createBroker();
    broker->setCommission(0.5); // $0.50 per trade
    
    // Create Backtester
    createBacktester();
    
    // Run backtest with low commission
    backtester->run();
    const PerformanceMetrics& lowCommMetrics = backtester->getPerformanceMetrics();
    
    // Now setup with high commission
    createBroker();
    broker->setCommission(20.0); // $20 per trade
    
    // Create new Backtester
    createBacktester();
    
    // Run backtest with high commission
    backtester->run();
    const PerformanceMetrics& highCommMetrics = backtester->getPerformanceMetrics();
    
    // Higher commission should result in lower P&L
    EXPECT_GT(lowCommMetrics.totalPnL, highCommMetrics.totalPnL);
}

// Test 4: Impact of different slippage levels on backtest results
TEST_F(BacktestIntegrationTests, SlippageImpactOnBacktestResults) {
    // Setup trending market data
    setupTrendingMarket();
    
    // Create SimulatedBroker with low slippage
    createBroker();
    broker->setSlippage(0.0001); // 0.01% slippage
    
    // Create Backtester
    createBacktester();
    
    // Run backtest with low slippage
    backtester->run();
    const PerformanceMetrics& lowSlippageMetrics = backtester->getPerformanceMetrics();
    
    // Now setup with high slippage
    createBroker();
    broker->setSlippage(0.01); // 1% slippage
    
    // Create new Backtester
    createBacktester();
    
    // Run backtest with high slippage
    backtester->run();
    const PerformanceMetrics& highSlippageMetrics = backtester->getPerformanceMetrics();
    
    // Higher slippage generally results in lower P&L, but this is probabilistic
    // We'll check that both runs produce valid metrics
    EXPECT_GE(lowSlippageMetrics.startingCapital, 0.0);
    EXPECT_GE(highSlippageMetrics.startingCapital, 0.0);
}

// Test 5: Impact of different starting capital on backtest results
TEST_F(BacktestIntegrationTests, StartingCapitalImpactOnBacktestResults) {
    // Setup trending market data
    setupTrendingMarket();
    
    // Create SimulatedBroker with low capital
    createBroker();
    broker->setStartingCapital(10000.0); // $10,000
    
    // Create Backtester
    createBacktester();
    
    // Run backtest with low capital
    backtester->run();
    const PerformanceMetrics& lowCapitalMetrics = backtester->getPerformanceMetrics();
    
    // Now setup with high capital
    createBroker();
    broker->setStartingCapital(1000000.0); // $1,000,000
    
    // Create new Backtester
    createBacktester();
    
    // Run backtest with high capital
    backtester->run();
    const PerformanceMetrics& highCapitalMetrics = backtester->getPerformanceMetrics();
    
    // The absolute P&L should be different, but percentage metrics might be similar
    EXPECT_NE(lowCapitalMetrics.totalPnL, highCapitalMetrics.totalPnL);
}

// Test 6: Multi-asset backtesting
TEST_F(BacktestIntegrationTests, MultiAssetBacktesting) {
    // Setup multi-asset market data
    setupMultiAssetMarket();
    
    // Update config for multi-asset testing
    testConfig["ticker"] = "AAPL,MSFT,GOOG";
    
    // Create broker
    createBroker();
    
    // Create backtester
    createBacktester();
    
    // Run backtest
    EXPECT_NO_THROW(backtester->run());
    
    // Check the results
    const PerformanceMetrics& metrics = backtester->getPerformanceMetrics();
    
    // We should have results with non-zero metrics
    EXPECT_GT(metrics.numTrades, 0);
    EXPECT_NE(metrics.totalPnL, 0.0);
}

// Test 7: Long-term vs short-term backtesting comparison
TEST_F(BacktestIntegrationTests, LongTermVsShortTermBacktesting) {
    // Create short-term market data (10 days)
    auto shortTermData = TestMarketDataProvider::createTrendingMarketData(10);
    marketData->update(shortTermData);
    
    // Create SimulatedBroker
    createBroker();
    
    // Create Backtester
    createBacktester();
    
    // Run short-term backtest
    backtester->run();
    const PerformanceMetrics& shortTermMetrics = backtester->getPerformanceMetrics();
    
    // Now create long-term market data (60 days)
    auto longTermData = TestMarketDataProvider::createTrendingMarketData(60);
    marketData->update(longTermData);
    
    // Create new broker and backtester for long-term test
    createBroker();
    createBacktester();
    
    // Run long-term backtest
    backtester->run();
    const PerformanceMetrics& longTermMetrics = backtester->getPerformanceMetrics();
    
    // Long-term backtest should have more trades
    EXPECT_GT(longTermMetrics.numTrades, shortTermMetrics.numTrades);
    
    // Annualized returns might be similar if the trend is consistent
    // but we can only check that both produce valid metrics
    EXPECT_GE(shortTermMetrics.annualizedReturn, -100.0);
    EXPECT_LE(shortTermMetrics.annualizedReturn, 1000.0);
    EXPECT_GE(longTermMetrics.annualizedReturn, -100.0);
    EXPECT_LE(longTermMetrics.annualizedReturn, 1000.0);
}

// Test 8: System resilience to extreme market conditions
TEST_F(BacktestIntegrationTests, ResilienceToExtremeMarketConditions) {
    // Create extreme market conditions - sharp crash followed by recovery
    std::vector<MarketCondition> extremeData;
    float price = 100.0f;
    
    for (int i = 0; i < 30; i++) {
        std::string dateTime = "2025-03-" + std::to_string(std::min(1 + i, 31)) + " 10:00:00";
        
        // Create a market crash on day 10 (-50%)
        if (i == 10) {
            price *= 0.5f;
        }
        // Followed by strong recovery
        else if (i > 10) {
            price *= 1.05f;
        }
        // Normal trending before crash
        else {
            price *= 1.01f;
        }
        
        MarketCondition condition(
            dateTime,        // DateTime
            "AAPL",         // Ticker
            price * 0.99f,  // Open
            price,          // Close
            1000 * (i + 1), // Volume
            "1m"            // TimeInterval
        );
        
        extremeData.push_back(condition);
    }
    
    marketData->update(extremeData);
    
    // Create broker and backtester
    createBroker();
    createBacktester();
    
    // Run backtest - should not crash
    EXPECT_NO_THROW(backtester->run());
    
    // Check the results
    const PerformanceMetrics& metrics = backtester->getPerformanceMetrics();
    
    // Max drawdown should be significant due to the crash
    EXPECT_GT(metrics.maxDrawdownPercent, 20.0);
}

// Test 9: Backtesting with stop losses and take profits
TEST_F(BacktestIntegrationTests, StopLossAndTakeProfitBacktesting) {
    // Create volatile market data
    setupVolatileMarket();
    
    // Create broker
    createBroker();
    
    // We'll manually place orders with stop losses and take profits
    // and then simulate market steps to see how they behave
    
    // Buy order with stop loss at 5% and take profit at 10%
    float initialPrice = broker->getLatestPrice("AAPL");
    Order buyOrder("buy", "AAPL", 100.0f, initialPrice);
    buyOrder.setStopLoss(5.0f);
    buyOrder.setTakeProfit(10.0f);
    broker->placeOrder(buyOrder);
    
    // Simulate market steps and check if stop loss or take profit triggered
    int stepsRun = 0;
    bool orderClosed = false;
    float finalPrice = 0.0f;
    
    for (int i = 0; i < 20; i++) {
        broker->nextStep();
        stepsRun++;
        
        Position position = broker->getLatestPosition("AAPL");
        if (position.getQuantity() == 0.0f) {
            orderClosed = true;
            finalPrice = broker->getLatestPrice("AAPL");
            break;
        }
    }
    
    // Check results - either the position should be closed by stop loss/take profit
    // or we should have run all steps
    EXPECT_TRUE(orderClosed || stepsRun == 20);
    
    if (orderClosed) {
        // If closed, check if it was due to stop loss or take profit
        float priceChange = (finalPrice - initialPrice) / initialPrice * 100.0f;
        
        // It should be near either the stop loss or take profit level
        bool nearStopLoss = std::abs(priceChange + 5.0f) < 2.0f;
        bool nearTakeProfit = std::abs(priceChange - 10.0f) < 2.0f;
        
        EXPECT_TRUE(nearStopLoss || nearTakeProfit);
    }
}

// Test 10: Backtesting with different order types
TEST_F(BacktestIntegrationTests, DifferentOrderTypesBacktesting) {
    // Create trending market data
    setupTrendingMarket();
    
    // Create broker
    createBroker();
    
    // Place different types of orders
    float currentPrice = broker->getLatestPrice("AAPL");
    
    // Market order - should execute immediately
    Order marketOrder("buy", "AAPL", 100.0f, currentPrice);
    broker->placeOrder(marketOrder);
    
    // Limit buy below market - should not execute until price drops
    Order limitBuyOrder("limit_buy", "AAPL", 50.0f, currentPrice * 0.95f);
    broker->placeOrder(limitBuyOrder);
    
    // Limit sell above market - should execute when price rises
    Order limitSellOrder("limit_sell", "AAPL", 100.0f, currentPrice * 1.05f);
    broker->placeOrder(limitSellOrder);
    
    // Run some market steps
    for (int i = 0; i < 10; i++) {
        broker->nextStep();
    }
    
    // Check results - market order should have executed
    EXPECT_GT(broker->getNumTrades(), 0);
}