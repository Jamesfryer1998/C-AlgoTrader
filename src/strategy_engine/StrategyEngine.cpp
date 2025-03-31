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
StrategyEngine::setUp(json configdata, StrategyFactory &stratFactory, MarketData &marketdata, BrokerBase* broker, bool Backtest)
{    
    std::cout << "Setting up Strategy Engine..." << std::endl;
    configData = configdata;
    oms->setUp(configData, broker);
    strategyList = stratFactory.generateStrategies();
    marketData = marketdata;
    backtest = Backtest;

    std::cout << "  --> Config Data set" << std::endl;
    std::cout << "  --> OMS set" << std::endl;
    std::cout << "  --> Broker set " << broker->brokerName << std::endl;
    std::cout << "  --> Strategies generated" << std::endl;
    std::cout << "  --> MarketData set\n" << std::endl;

    printStategies();
}

// This will run in a continuious loop somewhere, every min maybe?
void
StrategyEngine::run()
{
    // We don't need to reload the data every time
    // The MarketData object already has the data loaded
    // and is advanced by next() in backtesting mode
    if(!backtest)
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
    // std::cout << "Executing strategies..." << std::endl;

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