#include "StrategyEngine.hpp"
#include "StrategyFactory.hpp"

OrderManagement* StrategyEngine::oms = new OrderManagement();

StrategyEngine::StrategyEngine()
{
}

StrategyEngine::~StrategyEngine()
{
}


void 
StrategyEngine::setUp(json configdata, StrategyFactory &stratFactory, MarketData &marketdata)
{    
    std::cout << "Setting up Strategy Engine..." << std::endl;
    configData = configdata;
    oms->setConfig(configData);
    strategyList = stratFactory.generateStrategies();
    marketData = marketdata;
    printStategies();
}

// This will run in a continuious loop somewhere, every min maybe?
void
StrategyEngine::run()
{
    marketData.process(configData);
    setMarketData(marketData);
    oms->setMarketData(marketData);
    executeStrategies();
}

void
StrategyEngine::setMarketData(MarketData& inputData)
{
    marketConditions = inputData.getData();
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
    std::cout << "Loaded strategies:" << std::endl;

    for (auto& strat : strategyList) 
    {
        std::cout << "  -> "  << strat->_strategyAttribute.name << std::endl;
    }
}