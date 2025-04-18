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
        marketData.loadData(dataFilePath);
    }

    string stratFilePath = "/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/config_test.json";
    string dataFilePath = "/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/market_data_test_1.csv";
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

    EXPECT_EQ(rsi.getData().getData().size(), 10);
}

TEST_F(RSITests, ExecuteWorksCorreclty)
{
    auto strats = strategyFactory.generateStrategies();
    RSI rsi{strats[0].get()->_strategyAttribute};
    GenerateMarketData();

    rsi.supplyData(marketData);

    EXPECT_NO_THROW(rsi.execute());
}

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


// Following caluclations have been tested in Excel RSI formula to check againts
TEST_F(RSITests, Calculation_1)
{
    auto strats = strategyFactory.generateStrategies();
    strats[0].get()->_strategyAttribute.period = 14;
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {109,187,167,178,167,176,148,154,134,176,123,145,165,122};

    EXPECT_EQ(51.79f, rsi.calculateRSI(closes));
}

TEST_F(RSITests, Calculation_2)
{
    auto strats = strategyFactory.generateStrategies();
    strats[0].get()->_strategyAttribute.period = 7;
    RSI rsi{strats[0].get()->_strategyAttribute};

    std::vector<float> closes = {25.5, 26.0, 26.5, 27.1, 27.8, 27.9, 28.2, 27.3};

    EXPECT_EQ(63.79f, rsi.calculateRSI(closes));
}