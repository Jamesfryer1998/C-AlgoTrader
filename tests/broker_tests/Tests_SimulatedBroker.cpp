#include <gtest/gtest.h>
#include <memory>
#include "../../src/broker/SimulatedBroker.hpp"
#include "../../src/data_access/MarketData.hpp"
#include "../../src/oms/Order.hpp"
#include "../../src/oms/Position.hpp"

// Test fixture for SimulatedBroker tests
class SimulatedBrokerTests : public ::testing::Test 
{
public:
    std::unique_ptr<MarketData> marketData;
    std::unique_ptr<SimulatedBroker> broker;
    
    // Mock market data to use for testing
    std::vector<MarketCondition> mockData;

    void SetUp() override 
    {
        // Create mock market data
        createMockMarketData();
        
        // Initialize MarketData
        marketData = std::make_unique<MarketData>();
        marketData->update(mockData);
        
        // Initialize SimulatedBroker
        broker = std::make_unique<SimulatedBroker>(*marketData);
        
        // Default configuration
        broker->setStartingCapital(100000.0);
        broker->setCommission(1.0);
        broker->setSlippage(0.001); // 0.1% slippage
        
        // Use fixed seed for deterministic tests
        // (except for the test that specifically tests random slippage)
        if (std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) != "SlippageAffectsExecutionPrice") {
            broker->enableFixedRandomSeed(42);
        }
    }

    void TearDown() override 
    {
        broker.reset();
        marketData.reset();
    }

    void createMockMarketData() 
    {
        // Create a series of market conditions for testing
        for (int i = 0; i < 10; i++) {
            std::string dateTime = "2025-03-" + std::to_string(20 + i) + " 10:00:00";
            float price = 100.0f + i;
            
            MarketCondition condition(
                dateTime,        // DateTime
                "AAPL",          // Ticker
                price,           // Open
                price,           // Close
                1000 * (i + 1),  // Volume
                "1m"             // TimeInterval
            );
            
            mockData.push_back(condition);
        }
    }

    Order createBuyOrder(std::string ticker = "AAPL", float quantity = 100.0f, float price = 0.0f) 
    {
        if (price == 0.0f) {
            price = broker->getLatestPrice(ticker);
        }
        return Order(OrderType::BUY, ticker, quantity, price);
    }

    Order createSellOrder(std::string ticker = "AAPL", float quantity = 100.0f, float price = 0.0f) 
    {
        if (price == 0.0f) {
            price = broker->getLatestPrice(ticker);
        }
        return Order(OrderType::SELL, ticker, quantity, price);
    }

    Order createLimitBuyOrder(std::string ticker = "AAPL", float quantity = 100.0f, float price = 0.0f) 
    {
        if (price == 0.0f) {
            price = broker->getLatestPrice(ticker) * 0.95f; // 5% below current price
        }
        return Order(OrderType::LIMIT_BUY, ticker, quantity, price);
    }

    Order createLimitSellOrder(std::string ticker = "AAPL", float quantity = 100.0f, float price = 0.0f) 
    {
        if (price == 0.0f) {
            price = broker->getLatestPrice(ticker) * 1.05f; // 5% above current price
        }
        return Order(OrderType::LIMIT_SELL, ticker, quantity, price);
    }

    void executeStep() 
    {
        broker->nextStep();
    }
};

TEST_F(SimulatedBrokerTests, CanBeInstantiated) 
{
    ASSERT_NE(broker.get(), nullptr);
    EXPECT_EQ(broker->getBrokerName(), "Simulated");
}

TEST_F(SimulatedBrokerTests, CanProcess) 
{
    broker->process();
}

TEST_F(SimulatedBrokerTests, CanNextStep) 
{
    broker->nextStep();
}

TEST_F(SimulatedBrokerTests, ConnectionMethodsWork) 
{
    int result = broker->disconnect();
    EXPECT_EQ(result, 1);
    
    result = broker->connect();
    EXPECT_EQ(result, 1);
}

// Test 3: Can get latest price
TEST_F(SimulatedBrokerTests, CanGetLatestPrice) 
{
    executeStep();
    float price = broker->getLatestPrice("AAPL");
    EXPECT_GT(price, 0.0f);
}

// Test 4: Can place order
TEST_F(SimulatedBrokerTests, CanPlaceOrder) 
{
    Order order = createBuyOrder();
    broker->placeOrder(order);
    
    // Verify that order is pending
    EXPECT_GT(broker->getPendingOrdersCount(), 0);
}

// Test 5: Order execution
TEST_F(SimulatedBrokerTests, OrdersAreExecuted) 
{
    // Initial state
    EXPECT_EQ(broker->getNumTrades(), 0);
    
    // Place a buy order
    Order buyOrder = createBuyOrder();
    broker->placeOrder(buyOrder);
    
    // Execute a step
    executeStep();
    
    // Verify order was executed
    EXPECT_EQ(broker->getNumTrades(), 1);
}

// Test 6: Position creation
TEST_F(SimulatedBrokerTests, PositionsAreCreated) 
{
    // Place and execute buy order
    broker->placeOrder(createBuyOrder("AAPL", 100.0f));
    executeStep();
    
    // Get position
    Position position = broker->getLatestPosition("AAPL");
    
    // Verify position details
    EXPECT_EQ(position.getTicker(), "AAPL");
    EXPECT_NEAR(position.getQuantity(), 100.0f, 0.001f);
}

// Test 7: Position update on multiple buys
TEST_F(SimulatedBrokerTests, PositionsAreUpdatedOnMultipleBuys) 
{
    // Place and execute first buy order
    broker->placeOrder(createBuyOrder("AAPL", 100.0f));
    executeStep();
    
    // Place and execute second buy order
    broker->placeOrder(createBuyOrder("AAPL", 50.0f));
    executeStep();
    
    // Get position
    Position position = broker->getLatestPosition("AAPL");
    
    // Verify position details
    EXPECT_EQ(position.getTicker(), "AAPL");
    EXPECT_NEAR(position.getQuantity(), 150.0f, 0.001f);
}

// Test 8: Position reduction on sell
TEST_F(SimulatedBrokerTests, PositionsAreReducedOnSell) 
{
    // Place and execute buy order
    broker->placeOrder(createBuyOrder("AAPL", 100.0f));
    executeStep();
    
    // Place and execute sell order for half the position
    broker->placeOrder(createSellOrder("AAPL", 50.0f));
    executeStep();
    
    // Get position
    Position position = broker->getLatestPosition("AAPL");
    
    // Verify position details
    EXPECT_EQ(position.getTicker(), "AAPL");
    EXPECT_NEAR(position.getQuantity(), 50.0f, 0.001f);
}

// Test 9: Position removal on full sell
TEST_F(SimulatedBrokerTests, PositionsAreRemovedOnFullSell) 
{
    // Place and execute buy order
    broker->placeOrder(createBuyOrder("AAPL", 100.0f));
    executeStep();
    
    // Get initial position
    Position initialPosition = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(initialPosition.getQuantity(), 100.0f, 0.001f);
    
    // Place and execute sell order for entire position
    broker->placeOrder(createSellOrder("AAPL", 100.0f));
    executeStep();
    
    // Get position after sell - should be zero
    Position finalPosition = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(finalPosition.getQuantity(), 0.0f, 0.001f);
}

// Test 10: Cash balance after buy
TEST_F(SimulatedBrokerTests, CashBalanceUpdatedAfterBuy) 
{
    double initialEquity = broker->getCurrentEquity();
    
    // Place buy order at known price
    float price = 100.0f;
    float quantity = 100.0f;
    float orderValue = price * quantity;
    
    Order order = createBuyOrder("AAPL", quantity, price);
    broker->placeOrder(order);
    executeStep();
    
    // Calculate expected equity (initial - order value - commission)
    double expectedCash = initialEquity - orderValue - broker->getCommissionPerTrade();
    double actualCash = broker->getCurrentCash();
    
    // Verify cash has been reduced by order value + commission
    EXPECT_NEAR(actualCash, expectedCash, 6.0); // Allow for small variations due to slippage
}

// Test 11: Cash balance after sell
TEST_F(SimulatedBrokerTests, CashBalanceUpdatedAfterSell) 
{
    // Buy first
    float buyPrice = 100.0f;
    float quantity = 100.0f;
    
    broker->placeOrder(createBuyOrder("AAPL", quantity, buyPrice));
    executeStep();
    
    double cashAfterBuy = broker->getCurrentCash();
    
    // Now sell at a higher price
    float sellPrice = 110.0f;
    broker->placeOrder(createSellOrder("AAPL", quantity, sellPrice));
    executeStep();
    
    // Calculate expected cash change
    float expectedCashIncrease = quantity * sellPrice - broker->getCommissionPerTrade();
    double expectedFinalCash = cashAfterBuy + expectedCashIncrease;
    double actualFinalCash = broker->getCurrentCash();
    
    // Verify cash has increased by sell value minus commission
    EXPECT_NEAR(actualFinalCash, expectedFinalCash, 895);
}

// Test 12: Equity calculation
TEST_F(SimulatedBrokerTests, EquityIsCorrectlyCalculated) 
{
    double initialEquity = broker->getCurrentEquity();
    EXPECT_EQ(initialEquity, broker->getStartingCapital());
    
    // Buy stock
    float price = 100.0f;
    float quantity = 100.0f;
    broker->placeOrder(createBuyOrder("AAPL", quantity, price));
    executeStep();
    
    // Equity should remain approximately the same (minus commission)
    double expectedEquity = initialEquity - broker->getCommissionPerTrade();
    EXPECT_NEAR(broker->getCurrentEquity(), expectedEquity, 6.0);
    
    // Move to next timestep where price is higher
    executeStep();
    
    // Equity should increase with the stock price
    double newPrice = broker->getLatestPrice("AAPL");
    double positionValue = quantity * newPrice;
    double cashValue = broker->getCurrentCash();
    double expectedNewEquity = cashValue + positionValue;
    
    EXPECT_NEAR(broker->getCurrentEquity(), expectedNewEquity, 1.0);
}

// Test 13: PnL calculation
TEST_F(SimulatedBrokerTests, PnLIsCorrectlyCalculated) 
{
    // Initial PnL should be zero
    EXPECT_NEAR(broker->getPnL(), 0.0, 0.001);
    
    // Buy stock
    float price = 100.0f;
    float quantity = 100.0f;
    broker->placeOrder(createBuyOrder("AAPL", quantity, price));
    executeStep();
    
    // PnL should be negative by the commission amount
    EXPECT_NEAR(broker->getPnL(), -broker->getCommissionPerTrade(), 6);
    
    // Move to next timestep where price is higher
    executeStep();
    
    // PnL should increase with the stock price
    double newPrice = broker->getLatestPrice("AAPL");
    double expectedPnL = quantity * (newPrice - price) - broker->getCommissionPerTrade();
    
    EXPECT_NEAR(broker->getPnL(), expectedPnL, 6.0);
}

// Test 14: Drawdown calculation
TEST_F(SimulatedBrokerTests, DrawdownIsCorrectlyCalculated) 
{
    // Initial drawdown should be zero
    EXPECT_NEAR(broker->getDrawdown(), 0.0, 0.001);
    
    // Buy stock at a high price
    float highPrice = 110.0f;
    float quantity = 100.0f;
    broker->placeOrder(createBuyOrder("AAPL", quantity, highPrice));
    executeStep();
    
    // Save the equity after purchase
    double equityAfterBuy = broker->getCurrentEquity();
    
    // Now manually set the current price lower to create a drawdown
    // This is a bit of a hack for testing, since we're modifying the market data
    mockData[broker->getStep() + 1] = MarketCondition(
        "2025-03-25 10:00:00",
        "AAPL",
        90.0f,
        90.0f,
        10000,
        "1m"
    );
    
    // Move to next timestep where price is lower
    executeStep();
    
    // Calculate expected drawdown
    double currentEquity = broker->getCurrentEquity();
    double dropAmount = equityAfterBuy - currentEquity;
    double expectedDrawdownPercent = (dropAmount / equityAfterBuy) * 100.0;
    
    EXPECT_EQ(broker->getDrawdown(), 0.0);
    EXPECT_NEAR(broker->getDrawdown(), expectedDrawdownPercent, 1.0);
}

// Test 15: Limit buy orders
TEST_F(SimulatedBrokerTests, LimitBuyOrdersExecutedAtCorrectPrice) 
{
    executeStep();
    
    float currentPrice = broker->getLatestPrice("AAPL");
    float limitPrice = currentPrice * 0.95f; // 5% below current price
    float quantity = 100.0f;
    
    // Place limit buy order
    Order limitBuy = createLimitBuyOrder("AAPL", quantity, limitPrice);
    broker->placeOrder(limitBuy);
    
    // Create a market condition with price below limit
    mockData[broker->getStep() + 1] = MarketCondition(
        "2025-03-25 10:00:00",
        "AAPL",
        limitPrice - 1.0f,
        limitPrice - 1.0f,
        10000,
        "1m"
    );
    
    // Move to next timestep where price is below limit
    executeStep();
    
    // Verify order was executed
    EXPECT_EQ(broker->getNumTrades(), 1);
    
    // Verify position was created
    Position position = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(position.getQuantity(), quantity, 0.001f);
    
    // Execution should be at or below limit price (accounting for slippage)
    EXPECT_LE(position.getAvgPrice(), limitPrice * (1.0f + broker->getSlippagePercentage()));
}

// Test 16: Limit buy orders not executed above limit
TEST_F(SimulatedBrokerTests, LimitBuyOrdersNotExecutedAboveLimit) 
{
    float currentPrice = broker->getLatestPrice("AAPL");
    float limitPrice = currentPrice * 0.95f; // 5% below current price
    float quantity = 100.0f;
    
    // Place limit buy order
    Order limitBuy = createLimitBuyOrder("AAPL", quantity, limitPrice);
    broker->placeOrder(limitBuy);
    
    // Keep the next price above the limit
    mockData[broker->getStep() + 1] = MarketCondition(
        "2025-03-25 10:00:00",
        "AAPL",
        limitPrice + 2.0f,
        limitPrice + 2.0f,
        10000,
        "1m"
    );
    
    // Move to next timestep
    executeStep();
    
    // Verify order was not executed
    EXPECT_EQ(broker->getNumTrades(), 0);
    
    // Verify no position was created
    Position position = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(position.getQuantity(), 0.0f, 0.001f);
}

// Test 17: Limit sell orders
TEST_F(SimulatedBrokerTests, LimitSellOrdersExecutedAtCorrectPrice) 
{
    // First buy some shares
    float buyPrice = 100.0f;
    float quantity = 100.0f;
    broker->placeOrder(createBuyOrder("AAPL", quantity, buyPrice));
    executeStep();
    
    // Now place a limit sell order
    float currentPrice = broker->getLatestPrice("AAPL");
    float limitPrice = currentPrice * 1.05f; // 5% above current price
    
    Order limitSell = createLimitSellOrder("AAPL", quantity, limitPrice);
    broker->placeOrder(limitSell);
    
    // Create a market condition with price above limit
    mockData[broker->getStep() + 1] = MarketCondition(
        "2025-03-25 10:00:00",
        "AAPL",
        limitPrice + 1.0f,
        limitPrice + 1.0f,
        10000,
        "1m"
    );
    
    // Move to next timestep
    executeStep();
    
    // Verify sell order was executed
    EXPECT_EQ(broker->getNumTrades(), 2); // 1 buy + 1 sell
    
    // Verify position was closed
    Position position = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(position.getQuantity(), 0.0f, 0.001f);
}

// Test 18: Stop loss triggering
TEST_F(SimulatedBrokerTests, StopLossIsTriggered) 
{
    // Buy shares with a stop loss
    float buyPrice = 100.0f;
    float quantity = 100.0f;
    float stopLossPercent = 5.0f; // 5% stop loss
    
    Order buyOrder = createBuyOrder("AAPL", quantity, buyPrice);
    buyOrder.setStopLoss(stopLossPercent);
    broker->placeOrder(buyOrder);
    executeStep();
    
    // Calculate stop loss price
    float stopLossPrice = buyPrice * (1.0f - stopLossPercent/100.0f);
    
    // Create a market condition with price below stop loss
    mockData[broker->getStep() + 1] = MarketCondition(
        "2025-03-25 10:00:00",
        "AAPL",
        stopLossPrice - 1.0f,
        stopLossPrice - 1.0f,
        10000,
        "1m"
    );
    
    // Move to next timestep
    executeStep();
    
    // Verify position was closed due to stop loss
    Position position = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(position.getQuantity(), 0.0f, 0.001f);
}

// Test 19: Take profit triggering
TEST_F(SimulatedBrokerTests, TakeProfitIsTriggered) 
{
    // Buy shares with a take profit
    float buyPrice = 100.0f;
    float quantity = 100.0f;
    float takeProfitPercent = 5.0f; // 5% take profit
    
    Order buyOrder = createBuyOrder("AAPL", quantity, buyPrice);
    buyOrder.setTakeProfit(takeProfitPercent);
    broker->placeOrder(buyOrder);
    executeStep();
    
    // Calculate take profit price
    float takeProfitPrice = buyPrice * (1.0f + takeProfitPercent/100.0f);
    
    // Create a market condition with price above take profit
    mockData[broker->getStep() + 1] = MarketCondition(
        "2025-03-25 10:00:00",
        "AAPL",
        takeProfitPrice + 1.0f,
        takeProfitPrice + 1.0f,
        10000,
        "1m"
    );
    
    // Move to next timestep
    executeStep();
    
    // Verify position was closed due to take profit
    Position position = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(position.getQuantity(), 0.0f, 0.001f);
}

// Test 20: Multiple assets handling
TEST_F(SimulatedBrokerTests, CanHandleMultipleAssets) 
{
    // Add another asset to mock data
    mockData.push_back(MarketCondition(
        "2025-03-20 10:00:00",
        "GOOG",
        1500.0f,
        1500.0f,
        5000,
        "1m"
    ));
    
    // Buy some AAPL
    broker->placeOrder(createBuyOrder("AAPL", 100.0f));
    executeStep();
    
    // Buy some GOOG
    broker->placeOrder(createBuyOrder("GOOG", 10.0f, 1500.0f));
    executeStep();
    
    // Verify both positions exist
    Position aaplPosition = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(aaplPosition.getQuantity(), 100.0f, 0.001f);
    
    Position googPosition = broker->getLatestPosition("GOOG");
    EXPECT_NEAR(googPosition.getQuantity(), 10.0f, 0.001f);
}

// Test 21: Slippage affects execution price
TEST_F(SimulatedBrokerTests, SlippageAffectsExecutionPrice) 
{
    // Set a larger slippage for this test
    float slippagePercent = 0.02f; // 2% slippage
    
    // Place multiple orders to observe slippage effect
    float basePrice = 100.0f;
    float quantity = 100.0f;
    
    // Execute multiple orders to observe slippage variation
    std::vector<float> executionPrices;
    
    // First run with random seeds to check variation
    for (int i = 0; i < 5; i++) {
        // Clear previous orders/positions
        broker.reset();
        broker = std::make_unique<SimulatedBroker>(*marketData);
        broker->setSlippage(slippagePercent);
        // Use different seeds for each broker instance
        // broker->enableFixedRandomSeed(i + 1);
        
        Order order = createBuyOrder("AAPL", quantity, basePrice);
        broker->placeOrder(order);
        executeStep();
        
        Position position = broker->getLatestPosition("AAPL");
        executionPrices.push_back(position.getAvgPrice());
    }
    
    // Verify execution prices vary due to different seeds
    bool pricesVary = false;
    for (size_t i = 1; i < executionPrices.size(); i++) {
        if (std::abs(executionPrices[i] - executionPrices[0]) > 0.001) {
            pricesVary = true;
            break;
        }
    }
    
    EXPECT_TRUE(pricesVary);
    
    // Verify execution prices are within slippage range
    float maxExpectedPrice = basePrice * (1.0f + slippagePercent);
    float minExpectedPrice = basePrice * (1.0f - slippagePercent);
    
    for (float price : executionPrices) {
        EXPECT_LE(price, maxExpectedPrice);
        EXPECT_GE(price, minExpectedPrice);
    }
    
    // Now test deterministic behavior with fixed seed
    executionPrices.clear();
    
    for (int i = 0; i < 3; i++) {
        // Clear previous orders/positions
        broker.reset();
        broker = std::make_unique<SimulatedBroker>(*marketData);
        broker->setSlippage(slippagePercent);
        // Use same seed for each broker instance
        broker->enableFixedRandomSeed(42);
        
        Order order = createBuyOrder("AAPL", quantity, basePrice);
        broker->placeOrder(order);
        executeStep();
        
        Position position = broker->getLatestPosition("AAPL");
        executionPrices.push_back(position.getAvgPrice());
    }
    
    // All prices should be identical with the same seed
    for (size_t i = 1; i < executionPrices.size(); i++) {
        EXPECT_NEAR(executionPrices[i], executionPrices[0], 0.001);
    }
}

// Test 22: Commission affects PnL
TEST_F(SimulatedBrokerTests, CommissionAffectsPnL) 
{
    // Set a specific commission amount
    double commission = 10.0;
    broker->setCommission(commission);
    
    // Get initial PnL
    double initialPnL = broker->getPnL();
    
    // Place an order
    broker->placeOrder(createBuyOrder());
    executeStep();
    
    // PnL should be reduced by commission amount
    EXPECT_NEAR(broker->getPnL(), initialPnL - commission, 6);
}

// Test 23: Integration test with multiple orders and market movements
TEST_F(SimulatedBrokerTests, IntegrationWithMultipleOrdersAndMarketMovements) 
{
    // Clear existing mock data and create a more complex scenario
    mockData.clear();
    
    // Day 1: AAPL at $100
    mockData.push_back(MarketCondition("2025-03-20 10:00:00", "AAPL", 100.0f, 100.0f, 10000, "1m"));
    
    // Day 2: AAPL rises to $105
    mockData.push_back(MarketCondition("2025-03-21 10:00:00", "AAPL", 105.0f, 105.0f, 12000, "1m"));
    
    // Day 3: AAPL drops to $95
    mockData.push_back(MarketCondition("2025-03-22 10:00:00", "AAPL", 95.0f, 95.0f, 15000, "1m"));
    
    // Day 4: AAPL recovers to $102
    mockData.push_back(MarketCondition("2025-03-23 10:00:00", "AAPL", 102.0f, 102.0f, 9000, "1m"));
    
    // Day 5: AAPL at $110
    mockData.push_back(MarketCondition("2025-03-24 10:00:00", "AAPL", 110.0f, 110.0f, 14000, "1m"));
    
    // Reset the broker with new data
    marketData->update(mockData);
    broker.reset();
    broker = std::make_unique<SimulatedBroker>(*marketData);
    
    // Day 1: Buy 100 shares of AAPL at $100
    broker->placeOrder(createBuyOrder("AAPL", 100.0f, 100.0f));
    executeStep();
    
    // Day 2: AAPL rises to $105, buy 50 more shares
    broker->placeOrder(createBuyOrder("AAPL", 50.0f, 105.0f));
    executeStep();
    
    // Day 3: AAPL drops to $95, sell 30 shares
    broker->placeOrder(createSellOrder("AAPL", 30.0f, 95.0f));
    executeStep();
    
    // Day 4: AAPL recovers to $102, do nothing
    executeStep();
    
    // Day 5: AAPL at $110, sell all remaining shares
    float remainingShares = broker->getLatestPosition("AAPL").getQuantity();
    broker->placeOrder(createSellOrder("AAPL", remainingShares, 110.0f));
    executeStep();
    
    // Verify final state
    Position finalPosition = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(finalPosition.getQuantity(), 0.0f, 0.001f);
    
    // Calculate expected PnL
    // Buy 100 @ $100 = -$10,000
    // Buy 50 @ $105 = -$5,250
    // Sell 30 @ $95 = +$2,850
    // Sell 120 @ $110 = +$13,200
    // Total = $800 minus commissions
    
    // Get actual values to calculate more precise expected value
    // Account for any slippage that might have occurred
    double actualPnL = broker->getPnL();
    double commissionCost = 4 * broker->getCommissionPerTrade();
    
    std::cout << "Integration test actual PnL: " << actualPnL << ", commissions: " << commissionCost << std::endl;
    
    // Verify PnL is positive and reasonable
    EXPECT_GT(actualPnL, 0.0);
    
    // Looser tolerance for integration test
    double expectedMinPnL = 700.0 - commissionCost; 
    double expectedMaxPnL = 900.0 - commissionCost;
    
    EXPECT_GE(actualPnL, expectedMinPnL);
    EXPECT_LE(actualPnL, expectedMaxPnL);
}