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
    printStategies();
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
    std::cout << "Executing strategies..." << std::endl;

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

void
StrategyEngine::printStategies()
{
    std::cout << "Loades strategies:" << std::endl;

    for (auto& strat : strategyList) 
    {
        std::cout << "  -> "  << strat->_strategyAttribute.name << std::endl;
    }
}