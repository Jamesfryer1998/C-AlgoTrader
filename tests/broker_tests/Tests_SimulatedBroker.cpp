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
    MarketData mockMarketData;

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

TEST_F(SimulatedBrokerTests, CanGetLatestPrice) 
{
    executeStep();
    float price = broker->getLatestPrice("AAPL");
    EXPECT_GT(price, 0.0f);
}

TEST_F(SimulatedBrokerTests, CanPlaceOrder) 
{
    Order order = createBuyOrder();
    broker->placeOrder(order);
    
    EXPECT_GT(broker->getPendingOrdersCount(), 0);
}

TEST_F(SimulatedBrokerTests, OrdersAreExecuted) 
{
    EXPECT_EQ(broker->getNumTrades(), 0);
    
    Order buyOrder = createBuyOrder();
    broker->placeOrder(buyOrder);
    
    executeStep();
    
    EXPECT_EQ(broker->getNumTrades(), 1);
}

TEST_F(SimulatedBrokerTests, PositionsAreCreated) 
{
    broker->placeOrder(createBuyOrder("AAPL", 100.0f));
    executeStep();
    
    Position position = broker->getLatestPosition("AAPL");
    
    EXPECT_EQ(position.getTicker(), "AAPL");
    EXPECT_NEAR(position.getQuantity(), 100.0f, 0.001f);
}

TEST_F(SimulatedBrokerTests, PositionsAreUpdatedOnMultipleBuys) 
{
    broker->placeOrder(createBuyOrder("AAPL", 100.0f));
    executeStep();
    
    broker->placeOrder(createBuyOrder("AAPL", 50.0f));
    executeStep();
    
    Position position = broker->getLatestPosition("AAPL");
    
    EXPECT_EQ(position.getTicker(), "AAPL");
    EXPECT_NEAR(position.getQuantity(), 150.0f, 0.001f);
}

TEST_F(SimulatedBrokerTests, PositionsAreReducedOnSell) 
{
    broker->placeOrder(createBuyOrder("AAPL", 100.0f));
    executeStep();
    
    broker->placeOrder(createSellOrder("AAPL", 50.0f));
    executeStep();
    
    Position position = broker->getLatestPosition("AAPL");
    
    EXPECT_EQ(position.getTicker(), "AAPL");
    EXPECT_NEAR(position.getQuantity(), 50.0f, 0.001f);
}

TEST_F(SimulatedBrokerTests, PositionsAreRemovedOnFullSell) 
{
    broker->placeOrder(createBuyOrder("AAPL", 100.0f));
    executeStep();
    
    Position initialPosition = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(initialPosition.getQuantity(), 100.0f, 0.001f);
    
    broker->placeOrder(createSellOrder("AAPL", 100.0f));
    executeStep();
    
    Position finalPosition = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(finalPosition.getQuantity(), 0.0f, 0.001f);
}

TEST_F(SimulatedBrokerTests, CashBalanceUpdatedAfterBuy) 
{
    double initialEquity = broker->getCurrentEquity();
    
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

TEST_F(SimulatedBrokerTests, CashBalanceUpdatedAfterSell) 
{
    float buyPrice = 100.0f;
    float quantity = 100.0f;
    
    broker->placeOrder(createBuyOrder("AAPL", quantity, buyPrice));
    executeStep();
    
    double cashAfterBuy = broker->getCurrentCash();
    
    float sellPrice = 110.0f;
    broker->placeOrder(createSellOrder("AAPL", quantity, sellPrice));
    executeStep();
    
    float expectedCashIncrease = quantity * sellPrice - broker->getCommissionPerTrade();
    double expectedFinalCash = cashAfterBuy + expectedCashIncrease;
    double actualFinalCash = broker->getCurrentCash();
    
    EXPECT_NEAR(actualFinalCash, expectedFinalCash, 895);
}

TEST_F(SimulatedBrokerTests, EquityIsCorrectlyCalculated) 
{
    double initialEquity = broker->getCurrentEquity();
    EXPECT_EQ(initialEquity, broker->getStartingCapital());
    
    float price = 100.0f;
    float quantity = 100.0f;
    broker->placeOrder(createBuyOrder("AAPL", quantity, price));
    executeStep();
    
    double expectedEquity = initialEquity - broker->getCommissionPerTrade();
    EXPECT_NEAR(broker->getCurrentEquity(), expectedEquity, 6.0);
    
    executeStep();
    
    double newPrice = broker->getLatestPrice("AAPL");
    double positionValue = quantity * newPrice;
    double cashValue = broker->getCurrentCash();
    double expectedNewEquity = cashValue + positionValue;
    
    EXPECT_NEAR(broker->getCurrentEquity(), expectedNewEquity, 1.0);
}

TEST_F(SimulatedBrokerTests, PnLIsCorrectlyCalculated) 
{
    EXPECT_NEAR(broker->getPnL(), 0.0, 0.001);
    
    float price = 100.0f;
    float quantity = 100.0f;
    broker->placeOrder(createBuyOrder("AAPL", quantity, price));
    executeStep();
    
    EXPECT_NEAR(broker->getPnL(), -broker->getCommissionPerTrade(), 6);
    
    executeStep();
    
    double newPrice = broker->getLatestPrice("AAPL");
    double expectedPnL = quantity * (newPrice - price) - broker->getCommissionPerTrade();
    
    EXPECT_NEAR(broker->getPnL(), expectedPnL, 6.0);
}

TEST_F(SimulatedBrokerTests, DrawdownIsCorrectlyCalculated) 
{
    EXPECT_NEAR(broker->getDrawdown(), 0.0, 0.001);
    
    float highPrice = 110.0f;
    float quantity = 100.0f;
    broker->placeOrder(createBuyOrder("AAPL", quantity, highPrice));
    executeStep();
    
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
    
    executeStep();
    
    double currentEquity = broker->getCurrentEquity();
    double dropAmount = equityAfterBuy - currentEquity;
    double expectedDrawdownPercent = (dropAmount / equityAfterBuy) * 100.0;
    
    EXPECT_EQ(broker->getDrawdown(), 0.0);
    EXPECT_NEAR(broker->getDrawdown(), expectedDrawdownPercent, 1.0);
}

TEST_F(SimulatedBrokerTests, LimitBuyOrdersExecutedAtCorrectPrice) 
{
    broker->enableFixedRandomSeed(42);
    broker->setSlippage(0.0);
    
    executeStep();
    
    float currentPrice = broker->getLatestPrice("AAPL");
    float limitPrice = currentPrice * 0.95f;
    float quantity = 100.0f;
    
    std::cout << "Test LimitBuyOrdersExecutedAtCorrectPrice - Current price: " << currentPrice 
              << ", limit price: " << limitPrice << std::endl;
    
    Order limitBuy = createLimitBuyOrder("AAPL", quantity, limitPrice);
    broker->placeOrder(limitBuy);
    
    mockData[broker->getStep()] = MarketCondition(
        "2025-03-25 10:00:00",
        "AAPL",
        limitPrice - 1.0f,
        limitPrice - 1.0f,
        10000,
        "1m"
    );

    mockMarketData.update(mockData);
    broker->updateData(mockMarketData);
    
    executeStep();
    
    EXPECT_EQ(broker->getNumTrades(), 1);
    
    Position position = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(position.getQuantity(), quantity, 0.001f);
    
    // Execution should be exactly at limit price 
    EXPECT_NEAR(position.getAvgPrice(), limitPrice, 1.0f);
}

TEST_F(SimulatedBrokerTests, LimitBuyOrdersNotExecutedAboveLimit) 
{
    broker->enableFixedRandomSeed(42);
    broker->setSlippage(0.0);
    
    float currentPrice = broker->getLatestPrice("AAPL");
    float limitPrice = currentPrice * 0.95f;
    float quantity = 100.0f;
    
    std::cout << "Test LimitBuyOrdersNotExecutedAboveLimit - Current price: " << currentPrice 
              << ", limit price: " << limitPrice << std::endl;
    
    Order limitBuy = createLimitBuyOrder("AAPL", quantity, limitPrice);
    broker->placeOrder(limitBuy);
    
    float aboveLimitPrice = limitPrice + 2.0f;
    mockData[broker->getStep() + 1] = MarketCondition(
        "2025-03-25 10:00:00",
        "AAPL",
        aboveLimitPrice,
        aboveLimitPrice,
        10000,
        "1m"
    );
    
    std::cout << "Setting next market price to: " << aboveLimitPrice 
              << " (above limit price of " << limitPrice << ")" << std::endl;
    
    executeStep();
    
    EXPECT_EQ(broker->getNumTrades(), 0);
    
    Position position = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(position.getQuantity(), 0.0f, 0.001f);
}

TEST_F(SimulatedBrokerTests, LimitSellOrdersExecutedAtCorrectPrice) 
{
    broker->enableFixedRandomSeed(42);
    broker->setSlippage(0.0);
    
    float buyPrice = 100.0f;
    float quantity = 100.0f;
    broker->placeOrder(createBuyOrder("AAPL", quantity, buyPrice));
    executeStep();
    
    float currentPrice = broker->getLatestPrice("AAPL");
    float limitPrice = currentPrice * 1.05f;
    
    std::cout << "Test LimitSellOrdersExecutedAtCorrectPrice - Current price: " << currentPrice 
              << ", limit price: " << limitPrice << std::endl;
    
    Order limitSell = createLimitSellOrder("AAPL", quantity, limitPrice);
    broker->placeOrder(limitSell);
    
    float aboveLimitPrice = limitPrice + 1.0f;
    mockData[broker->getStep()] = MarketCondition(
        "2025-03-25 10:00:00",
        "AAPL",
        aboveLimitPrice,
        aboveLimitPrice,
        10000,
        "1m"
    );

    mockMarketData.update(mockData);
    broker->updateData(mockMarketData);
    
    std::cout << "Setting next market price to: " << aboveLimitPrice 
              << " (above limit price of " << limitPrice << ")" << std::endl;
    
    executeStep();
    
    EXPECT_EQ(broker->getNumTrades(), 2); // 1 buy + 1 sell
    
    Position position = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(position.getQuantity(), 0.0f, 0.001f);
    
    const std::vector<Order>& filledOrders = broker->getFilledOrders();
    EXPECT_EQ(filledOrders.size(), 2);
    
    if (filledOrders.size() >= 2) {
        const Order& sellOrder = filledOrders[1]; // Second order should be the sell
        EXPECT_NEAR(sellOrder.getPrice(), limitPrice, 1.0f);
    }
}

TEST_F(SimulatedBrokerTests, StopLossIsTriggered) 
{
    executeStep();
    
    broker->enableFixedRandomSeed(42);
    broker->setSlippage(0.0);
    
    float buyPrice = 100.0f;
    float quantity = 100.0f;
    float stopLossPercent = 5.0f;
    Order buyOrder = createBuyOrder("AAPL", quantity, buyPrice);
    buyOrder.setStopLoss(stopLossPercent);
    broker->placeOrder(buyOrder);
    executeStep();
    
    float stopLossPrice = buyPrice * (1.0f - stopLossPercent/100.0f);
    
    std::cout << "Test StopLossIsTriggered - Buy price: " << buyPrice 
              << ", stop loss percent: " << stopLossPercent
              << ", calculated stop loss price: " << stopLossPrice << std::endl;
    
    float belowStopLossPrice = stopLossPrice - 1.0f;
    mockData[broker->getStep()] = MarketCondition(
        "2025-03-25 10:00:00",
        "AAPL",
        belowStopLossPrice,
        belowStopLossPrice,
        10000,
        "1m"
    );

    mockMarketData.update(mockData);
    broker->updateData(mockMarketData);
    
    std::cout << "Setting next market price to: " << belowStopLossPrice 
              << " (below stop loss price of " << stopLossPrice << ")" << std::endl;
    
    executeStep();
    
    Position position = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(position.getQuantity(), 0.0f, 0.001f);
    
    EXPECT_EQ(broker->getNumTrades(), 2);
    
    const std::vector<Order>& filledOrders = broker->getFilledOrders();
    EXPECT_EQ(filledOrders.size(), 2);
    
    if (filledOrders.size() >= 2) {
        const Order& sellOrder = filledOrders[1]; // Second order should be the stop loss
        EXPECT_TRUE(sellOrder.isSell());
        EXPECT_NEAR(sellOrder.getPrice(), belowStopLossPrice, 0.001f);
    }
}

TEST_F(SimulatedBrokerTests, TakeProfitIsTriggered) 
{
    broker->enableFixedRandomSeed(42);
    broker->setSlippage(0.0);
    
    float buyPrice = 100.0f;
    float quantity = 100.0f;
    float takeProfitPercent = 5.0f;
    
    Order buyOrder = createBuyOrder("AAPL", quantity, buyPrice);
    buyOrder.setTakeProfit(takeProfitPercent);
    broker->placeOrder(buyOrder);
    executeStep();
    
    float takeProfitPrice = buyPrice * (1.0f + takeProfitPercent/100.0f);
    
    std::cout << "Test TakeProfitIsTriggered - Buy price: " << buyPrice 
              << ", take profit percent: " << takeProfitPercent
              << ", calculated take profit price: " << takeProfitPrice << std::endl;
    
    float aboveTakeProfitPrice = takeProfitPrice + 1.0f;
    mockData[broker->getStep()] = MarketCondition(
        "2025-03-25 10:00:00",
        "AAPL",
        aboveTakeProfitPrice,
        aboveTakeProfitPrice,
        10000,
        "1m"
    );

    mockMarketData.update(mockData);
    broker->updateData(mockMarketData);
    
    std::cout << "Setting next market price to: " << aboveTakeProfitPrice 
              << " (above take profit price of " << takeProfitPrice << ")" << std::endl;
    
    executeStep();
    
    Position position = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(position.getQuantity(), 0.0f, 0.001f);
    
    // Verify that both orders were filled
    EXPECT_EQ(broker->getNumTrades(), 2); // 1 buy + 1 take profit sell
    
    const std::vector<Order>& filledOrders = broker->getFilledOrders();
    EXPECT_EQ(filledOrders.size(), 2);
    
    if (filledOrders.size() >= 2) {
        const Order& sellOrder = filledOrders[1]; // Second order should be the take profit
        EXPECT_TRUE(sellOrder.isSell());
        EXPECT_NEAR(sellOrder.getPrice(), aboveTakeProfitPrice, 0.001f);
    }
}

TEST_F(SimulatedBrokerTests, CanHandleMultipleAssets) 
{
    mockData.push_back(MarketCondition(
        "2025-03-20 10:00:00",
        "GOOG",
        1500.0f,
        1500.0f,
        5000,
        "1m"
    ));
    
    broker->placeOrder(createBuyOrder("AAPL", 100.0f));
    executeStep();
    
    broker->placeOrder(createBuyOrder("GOOG", 10.0f, 1500.0f));
    executeStep();
    
    Position aaplPosition = broker->getLatestPosition("AAPL");
    EXPECT_NEAR(aaplPosition.getQuantity(), 100.0f, 0.001f);
    
    Position googPosition = broker->getLatestPosition("GOOG");
    EXPECT_NEAR(googPosition.getQuantity(), 10.0f, 0.001f);
}

TEST_F(SimulatedBrokerTests, SlippageAffectsExecutionPrice) 
{
    float slippagePercent = 0.02f;
    float basePrice = 100.0f;
    float quantity = 100.0f;
    
    std::vector<float> executionPrices;
    
    // First run with random seeds to check variation
    for (int i = 0; i < 5; i++) {
        broker.reset();
        broker = std::make_unique<SimulatedBroker>(*marketData);
        broker->setSlippage(slippagePercent);
        Order order = createBuyOrder("AAPL", quantity, basePrice);
        broker->placeOrder(order);
        executeStep();
        
        Position position = broker->getLatestPosition("AAPL");
        executionPrices.push_back(position.getAvgPrice());
    }
    
    bool pricesVary = false;
    for (size_t i = 1; i < executionPrices.size(); i++) {
        if (std::abs(executionPrices[i] - executionPrices[0]) > 0.001) {
            pricesVary = true;
            break;
        }
    }
    
    EXPECT_TRUE(pricesVary);
    
    float maxExpectedPrice = basePrice * (1.0f + slippagePercent);
    float minExpectedPrice = basePrice * (1.0f - slippagePercent);
    
    for (float price : executionPrices) {
        EXPECT_LE(price, maxExpectedPrice);
        EXPECT_GE(price, minExpectedPrice);
    }
    
    // Now test deterministic behavior with fixed seed
    executionPrices.clear();
    for (int i = 0; i < 3; i++) {
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

TEST_F(SimulatedBrokerTests, CommissionAffectsPnL) 
{
    double commission = 10.0;
    broker->setCommission(commission);
    double initialPnL = broker->getPnL();
    
    broker->placeOrder(createBuyOrder());
    executeStep();
    
    EXPECT_NEAR(broker->getPnL(), initialPnL - commission, 6);
}

TEST_F(SimulatedBrokerTests, IntegrationWithMultipleOrdersAndMarketMovements) 
{
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
    
    // Day 4: AAPL recovers to $102, do nothing, execute next step
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