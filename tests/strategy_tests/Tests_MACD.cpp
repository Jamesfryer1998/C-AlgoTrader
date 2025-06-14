#include <gtest/gtest.h>
#include "../../src/strategy_engine/MACD.hpp"
#include "../../src/strategy_engine/StrategyFactory.hpp"

class MACDTests : public ::testing::Test {
public:

    void SetUp() override 
    {

    }

    void TearDown() override 
    {

    }

    void GenerateMarketData()
    {
        Config config;
        string dataFilePath = config.getTestPath("strategy_tests/test_data/market_data_test.csv");
        marketData.loadData(dataFilePath);
    }

    void GenerateMarketDataForSell()
    {
        Config config;
        string dataFilePath = config.getTestPath("strategy_tests/test_data/market_data_for_sell.csv");
        marketData.loadData(dataFilePath);
    }

    void GenerateMarketDataForBuy()
    {
        Config config;
        string dataFilePath = config.getTestPath("strategy_tests/test_data/market_data_for_buy_macd.csv");
        marketData.loadData(dataFilePath);
    }

    void GenerateMarketDataForHold()
    {
        Config config;
        string dataFilePath = config.getTestPath("strategy_tests/test_data/market_data_for_hold.csv");
        marketData.loadData(dataFilePath);
    }

    Config config;
    string stratFilePath = config.getTestPath("strategy_tests/test_data/config_test.json");
    MarketData marketData;
    StrategyFactory strategyFactory{stratFilePath};

};

TEST_F(MACDTests, CanInstantiate)
{
    auto strats = strategyFactory.generateStrategies();
    MACD macd{strats[1].get()->_strategyAttribute};
}

TEST_F(MACDTests, CanSupplyWithData)
{
    auto strats = strategyFactory.generateStrategies();
    MACD macd{strats[1].get()->_strategyAttribute};
    GenerateMarketData();

    macd.supplyData(marketData);

    EXPECT_EQ(macd.getData().getData().size(), 20);
}

TEST_F(MACDTests, ExecuteWorksCorrectly)
{
    auto strats = strategyFactory.generateStrategies();
    MACD macd{strats[1].get()->_strategyAttribute};
    GenerateMarketData();

    macd.supplyData(marketData);

    EXPECT_NO_THROW(macd.execute());
}

TEST_F(MACDTests, ExecuteGenerateABuyOrder)
{
    auto strats = strategyFactory.generateStrategies();
    MACD macd{strats[1].get()->_strategyAttribute};
    GenerateMarketDataForBuy();

    macd.supplyData(marketData);
    macd.execute();
    
    EXPECT_TRUE(macd.NewOrder);
    EXPECT_EQ("BUY", macd.order.getTypeAsString());
}

TEST_F(MACDTests, ExecuteGenerateASellOrder)
{
    auto strats = strategyFactory.generateStrategies();
    MACD macd{strats[1].get()->_strategyAttribute};
    GenerateMarketDataForSell();

    macd.supplyData(marketData);
    macd.execute();
    
    EXPECT_TRUE(macd.NewOrder);
    EXPECT_EQ("SELL", macd.order.getTypeAsString());
}

TEST_F(MACDTests, ExecuteGenerateAHold)
{
    auto strats = strategyFactory.generateStrategies();
    MACD macd{strats[1].get()->_strategyAttribute};
    GenerateMarketDataForHold();

    macd.supplyData(marketData);
    macd.execute();
    
    // We do not generate a order for a HOLD
    EXPECT_FALSE(macd.NewOrder);
}

// MACD Calculation Tests
TEST_F(MACDTests, EMACalculation_SimpleCase)
{
    auto strats = strategyFactory.generateStrategies();
    MACD macd{strats[1].get()->_strategyAttribute};
    
    std::vector<float> prices = {10.0f, 12.0f, 14.0f, 16.0f, 18.0f, 20.0f};
    std::vector<float> ema = macd.calculateEMA(prices, 3);
    
    // First EMA should be the first price
    EXPECT_EQ(ema[0], 10.0f);
    
    // Check that EMA is calculated (should be increasing trend)
    EXPECT_GT(ema[5], ema[0]);
    EXPECT_EQ(ema.size(), prices.size());
}

TEST_F(MACDTests, MACDCalculation_BasicFunctionality)
{
    auto strats = strategyFactory.generateStrategies();
    strats[1].get()->_strategyAttribute.short_period = 5;
    strats[1].get()->_strategyAttribute.long_period = 10;
    strats[1].get()->_strategyAttribute.signal_period = 3;
    MACD macd{strats[1].get()->_strategyAttribute};
    
    // Generate enough data points for calculation
    std::vector<float> prices;
    for(int i = 1; i <= 20; i++) {
        prices.push_back(100.0f + i * 0.5f); // Gradual upward trend
    }
    
    std::vector<MACDResult> results = macd.calculateMACD(prices);
    
    // Should have results
    EXPECT_GT(results.size(), 0);
    
    // Check that all components are calculated
    if(results.size() > 0) {
        MACDResult result = results.back();
        EXPECT_NE(result.macdLine, 0.0f);
        EXPECT_NE(result.signalLine, 0.0f);
        // Histogram should be MACD - Signal
        EXPECT_FLOAT_EQ(result.histogram, result.macdLine - result.signalLine);
    }
}

TEST_F(MACDTests, BullishCrossover_Detection)
{
    auto strats = strategyFactory.generateStrategies();
    MACD macd{strats[1].get()->_strategyAttribute};
    
    // Previous: Signal above MACD
    MACDResult previous;
    previous.macdLine = 1.0f;
    previous.signalLine = 2.0f;
    
    // Current: MACD crosses above Signal
    MACDResult current;
    current.macdLine = 2.5f;
    current.signalLine = 2.0f;
    
    EXPECT_TRUE(macd.isBullishCrossover(previous, current));
}

TEST_F(MACDTests, BearishCrossover_Detection)
{
    auto strats = strategyFactory.generateStrategies();
    MACD macd{strats[1].get()->_strategyAttribute};
    
    // Previous: MACD above Signal
    MACDResult previous;
    previous.macdLine = 2.0f;
    previous.signalLine = 1.0f;
    
    // Current: MACD crosses below Signal
    MACDResult current;
    current.macdLine = 0.5f;
    current.signalLine = 1.0f;
    
    EXPECT_TRUE(macd.isBearishCrossover(previous, current));
}

TEST_F(MACDTests, BullishZeroCross_Detection)
{
    auto strats = strategyFactory.generateStrategies();
    MACD macd{strats[1].get()->_strategyAttribute};
    
    // Previous: MACD below zero
    MACDResult previous;
    previous.macdLine = -1.0f;
    previous.signalLine = 0.0f;
    
    // Current: MACD crosses above zero
    MACDResult current;
    current.macdLine = 0.5f;
    current.signalLine = 0.0f;
    
    EXPECT_TRUE(macd.isBullishZeroCross(previous, current));
}

TEST_F(MACDTests, BearishZeroCross_Detection)
{
    auto strats = strategyFactory.generateStrategies();
    MACD macd{strats[1].get()->_strategyAttribute};
    
    // Previous: MACD above zero
    MACDResult previous;
    previous.macdLine = 1.0f;
    previous.signalLine = 0.0f;
    
    // Current: MACD crosses below zero
    MACDResult current;
    current.macdLine = -0.5f;
    current.signalLine = 0.0f;
    
    EXPECT_TRUE(macd.iBearishZeroCross(previous, current));
}

TEST_F(MACDTests, NoCrossover_Detection)
{
    auto strats = strategyFactory.generateStrategies();
    MACD macd{strats[1].get()->_strategyAttribute};
    
    // Both periods: MACD consistently above Signal
    MACDResult previous;
    previous.macdLine = 2.0f;
    previous.signalLine = 1.0f;
    
    MACDResult current;
    current.macdLine = 2.5f;
    current.signalLine = 1.5f;
    
    EXPECT_FALSE(macd.isBullishCrossover(previous, current));
    EXPECT_FALSE(macd.isBearishCrossover(previous, current));
}

TEST_F(MACDTests, InsufficientData_HandledGracefully)
{
    auto strats = strategyFactory.generateStrategies();
    strats[1].get()->_strategyAttribute.short_period = 12;
    strats[1].get()->_strategyAttribute.long_period = 26;
    strats[1].get()->_strategyAttribute.signal_period = 9;
    MACD macd{strats[1].get()->_strategyAttribute};
    
    // Only provide 10 data points when we need 26 + 9 = 35
    std::vector<float> prices = {100.0f, 101.0f, 102.0f, 103.0f, 104.0f, 
                                105.0f, 106.0f, 107.0f, 108.0f, 109.0f};
    
    std::vector<MACDResult> results = macd.calculateMACD(prices);
    
    // Should return empty results
    EXPECT_EQ(results.size(), 0);
}

TEST_F(MACDTests, Validation_RequiredParameters)
{
    auto strats = strategyFactory.generateStrategies();
    
    // Test missing short_period
    strats[1].get()->_strategyAttribute.short_period = 0;
    MACD macd1{strats[1].get()->_strategyAttribute};
    EXPECT_THROW(macd1.validate(), std::runtime_error);
    
    // Test missing long_period
    strats[1].get()->_strategyAttribute.short_period = 12;
    strats[1].get()->_strategyAttribute.long_period = 0;
    MACD macd2{strats[1].get()->_strategyAttribute};
    EXPECT_THROW(macd2.validate(), std::runtime_error);
    
    // Test missing signal_period
    strats[1].get()->_strategyAttribute.long_period = 26;
    strats[1].get()->_strategyAttribute.signal_period = 0;
    MACD macd3{strats[1].get()->_strategyAttribute};
    EXPECT_THROW(macd3.validate(), std::runtime_error);
}