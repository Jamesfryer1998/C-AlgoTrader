#include <gtest/gtest.h>
#include "../../src/strategy_engine/RSI.hpp"
#include "../../src/strategy_engine/StrategyFactory.hpp"

class RSITests : public ::testing::Test {
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
        string dataFilePath = config.getTestPath("strategy_tests/test_data/market_data_for_buy.csv");
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

TEST_F(RSITests, CanInstantiate)
{
    auto strats = strategyFactory.generateStrategies();
    RSI rsi{strats[0].get()->_strategyAttribute};
}

TEST_F(RSITests, CanSupplyWithData)
{
    auto strats = strategyFactory.generateStrategies();
    RSI rsi{strats[0].get()->_strategyAttribute};
    GenerateMarketData();

    rsi.supplyData(marketData);

    EXPECT_EQ(rsi.getData().getData().size(), 20);
}

TEST_F(RSITests, ExecuteWorksCorreclty)
{
    auto strats = strategyFactory.generateStrategies();
    RSI rsi{strats[0].get()->_strategyAttribute};
    GenerateMarketData();

    rsi.supplyData(marketData);

    EXPECT_NO_THROW(rsi.execute());
}

TEST_F(RSITests, ExecuteGenerateABuyOrder)
{
    auto strats = strategyFactory.generateStrategies();
    RSI rsi{strats[0].get()->_strategyAttribute};
    GenerateMarketDataForBuy();

    rsi.supplyData(marketData);
    rsi.execute();
    
    EXPECT_TRUE(rsi.NewOrder);
    EXPECT_EQ("BUY", rsi.order.getTypeAsString());
}

TEST_F(RSITests, ExecuteGenerateASellOrder)
{
    auto strats = strategyFactory.generateStrategies();
    RSI rsi{strats[0].get()->_strategyAttribute};
    GenerateMarketDataForSell();

    rsi.supplyData(marketData);
    rsi.execute();
    
    EXPECT_TRUE(rsi.NewOrder);
    EXPECT_EQ("SELL", rsi.order.getTypeAsString());
}

TEST_F(RSITests, ExecuteGenerateAHold)
{
    auto strats = strategyFactory.generateStrategies();
    RSI rsi{strats[0].get()->_strategyAttribute};
    GenerateMarketDataForHold();

    rsi.supplyData(marketData);
    rsi.execute();
    
    // We do not generate a order for a HOLD
    EXPECT_FALSE(rsi.NewOrder);
}


// Calculate RSI
// Following calculations have been tested in Excel and talib RSI formula to check against
TEST_F(RSITests, CalculateCorrectRSIForConstantIncreaseOverPeriod)
{
    auto strats = strategyFactory.generateStrategies();
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

    EXPECT_EQ(100, rsi.calculateRSI(closes));
}

TEST_F(RSITests, CalculateCorrectRSIForConstantDecreaseOverPeriod)
{
    auto strats = strategyFactory.generateStrategies();
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1};

    EXPECT_EQ(0, rsi.calculateRSI(closes));
}

TEST_F(RSITests, CalculateCorrectRSIForNeuturalPriceOverPeriod)
{
    auto strats = strategyFactory.generateStrategies();
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};

    EXPECT_EQ(50, rsi.calculateRSI(closes));
}

TEST_F(RSITests, CalculateCorrectRSIForEqualRiseAndFallOverPeriod)
{
    auto strats = strategyFactory.generateStrategies();
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {1,2,3,4,5,6,7,8,7,6,5,4,3,2,1};

    EXPECT_EQ(50, rsi.calculateRSI(closes));
}

TEST_F(RSITests, TooFewPointsReturnsNeutural)
{
    auto strats = strategyFactory.generateStrategies();
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {1};

    EXPECT_EQ(50, rsi.calculateRSI(closes));
}

TEST_F(RSITests, Calculation_1)
{
    auto strats = strategyFactory.generateStrategies();
    strats[0].get()->_strategyAttribute.period = 14;
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {109,187,167,178,167,176,148,154,134,176,123,145,165,122,122};

    EXPECT_EQ(51.79f, rsi.calculateRSI(closes));
}

TEST_F(RSITests, Calculation_2)
{
    auto strats = strategyFactory.generateStrategies();
    strats[0].get()->_strategyAttribute.period = 7;
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {25.5f, 26.0f, 26.5f, 27.1f, 27.8f, 27.9f, 28.2f, 27.3f};

    EXPECT_EQ(75.0f, rsi.calculateRSI(closes));
}

TEST_F(RSITests, Calculation_3)
{
    auto strats = strategyFactory.generateStrategies();
    strats[0].get()->_strategyAttribute.period = 14;
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {45.0, 45.25, 45.10, 45.64, 46.25, 46.50, 46.75, 47.21, 47.42, 47.35, 47.15, 46.95, 47.65, 48.12, 48.55};

    EXPECT_EQ(87.06f, rsi.calculateRSI(closes));
}

TEST_F(RSITests, Calculation_4)
{
    auto strats = strategyFactory.generateStrategies();
    strats[0].get()->_strategyAttribute.period = 5;
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {105.6, 104.7, 103.8, 102.9, 101.5, 100.2};

    EXPECT_EQ(0.00f, rsi.calculateRSI(closes));
}

TEST_F(RSITests, Calculation_5)
{
    auto strats = strategyFactory.generateStrategies();
    strats[0].get()->_strategyAttribute.period = 10;
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {67.2, 67.2, 67.2, 67.2, 67.2, 67.2, 67.2, 67.2, 67.2, 67.2, 67.2};

    EXPECT_EQ(50.00f, rsi.calculateRSI(closes));
}

TEST_F(RSITests, Calculation_6)
{
    auto strats = strategyFactory.generateStrategies();
    strats[0].get()->_strategyAttribute.period = 9;
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {34.5, 35.2, 36.1, 35.8, 35.9, 36.7, 37.2, 38.1, 39.2, 40.1};

    EXPECT_EQ(95.16f, rsi.calculateRSI(closes));
}

TEST_F(RSITests, Calculation_7)
{
    auto strats = strategyFactory.generateStrategies();
    strats[0].get()->_strategyAttribute.period = 21;
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {128.6, 129.2, 130.5, 131.4, 132.8, 133.7, 132.5, 131.8, 132.4, 
                                133.5, 134.6, 135.7, 136.9, 137.5, 136.8, 137.1, 138.6, 139.2, 
                                138.5, 137.9, 138.4, 139.2};

    EXPECT_EQ(78.80f, rsi.calculateRSI(closes));
}

TEST_F(RSITests, Calculation_8)
{
    auto strats = strategyFactory.generateStrategies();
    strats[0].get()->_strategyAttribute.period = 3;
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {243.8, 242.5, 241.3, 240.1};

    EXPECT_EQ(0.00f, rsi.calculateRSI(closes));
}

TEST_F(RSITests, Calculation_9)
{
    auto strats = strategyFactory.generateStrategies();
    strats[0].get()->_strategyAttribute.period = 6;
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {55.72, 55.63, 55.95, 56.42, 56.89, 57.11, 57.63};

    EXPECT_EQ(95.69f, rsi.calculateRSI(closes));
}

TEST_F(RSITests, Calculation_10)
{
    auto strats = strategyFactory.generateStrategies();
    strats[0].get()->_strategyAttribute.period = 14;
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {425.5, 424.8, 425.3, 426.7, 428.1, 429.5, 428.7, 429.2, 430.6, 
                                431.2, 432.5, 433.1, 432.4, 431.5, 432.7};

    EXPECT_EQ(76.87f, rsi.calculateRSI(closes));
}