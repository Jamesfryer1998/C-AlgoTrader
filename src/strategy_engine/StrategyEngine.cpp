#include "StrategyEngine.hpp"
#include "StrategyFactory.hpp"

StrategyEngine::StrategyEngine()
{
}

StrategyEngine::~StrategyEngine()
{
}


void 
StrategyEngine::setUp()
{
    std::cout << "Setting up Strategy Engine..." << std::endl;
    generateAndLoadStrategies();

    std::cout << strategyList.size() << " strategies loaded" << std::endl;
}

// This will run in a continuious loop somewhere, every min maybe?
void
StrategyEngine::run()
{
    MarketData marketData;
    marketData.process();

    inputMarketData(marketData);
    executeStrategies();
}

void
StrategyEngine::inputMarketData(MarketData& inputData)
{
    marketData = inputData.getData();
}

void
StrategyEngine::generateAndLoadStrategies()
{
    StrategyFactory stratFactory;
    strategyList = stratFactory.generateStrategies();
}

void
StrategyEngine::executeStrategies()
{
    // Maybe thread this
    for (auto& strat : strategyList) 
    {
        strat->execute();
        if(strat->onNewOrder())
        {
            Order order = strat->getOrder();

            // Give Order to OMS
            oms->onNewOrder(order);
        }
    }
}