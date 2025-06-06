#include "StrategyEngine.hpp"
#include "StrategyFactory.hpp"

OrderManagement* StrategyEngine::oms = new OrderManagement();

StrategyEngine::StrategyEngine()
: marketData(nullptr)
{
}

StrategyEngine::~StrategyEngine()
{
    // We don't own the marketData pointer, so don't delete it
}


void 
StrategyEngine::setUp(json configdata, StrategyFactory &stratFactory, MarketData &marketdata, BrokerBase* broker)
{    
    std::cout << "Setting up Strategy Engine..." << std::endl;
    configData = configdata;
    oms->setUp(configData, broker);
    strategyList = stratFactory.generateStrategies();
    marketData = &marketdata; // Store a pointer to the MarketData object

    std::cout << "  --> Config Data set" << std::endl;
    std::cout << "  --> OMS set" << std::endl;
    std::cout << "  --> Broker set " << broker->brokerName << std::endl;
    std::cout << "  --> Strategies generated" << std::endl;
    std::cout << "  --> MarketData set\n" << std::endl;

    printStategies();
}

void
StrategyEngine::run()
{
    // Check if marketData pointer is valid
    if (marketData == nullptr) {
        std::cerr << "ERROR: marketData pointer is null in StrategyEngine::run()" << std::endl;
        return;
    }
    
    // Update the market data with the latest data
    // In live mode, this will fetch new data
    // In backtest mode, the data is updated by the adapter
    setMarketData(*marketData);
    oms->setMarketData(*marketData);
    
    // Execute all active strategies
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
    Order order;
    std::vector<Order> proposedOrders;
    
    // Check if marketData pointer is valid
    if (marketData == nullptr) {
        std::cerr << "ERROR: marketData pointer is null in StrategyEngine::executeStrategies()" << std::endl;
        return;
    }

    // Maybe thread this, thread for each strategy
    for (auto& strat : strategyList) 
    {
        strat->supplyData(*marketData);
        strat->execute();

        if(strat->onNewOrder())
        {
            Order order = strat->getOrder();
            // proposedOrders.push_back(order);
            oms->onNewOrder(order);
        }
    }

    // if(DecideToMakeTrade(proposedOrders))
    //     oms->onNewOrder(order);

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

bool
StrategyEngine::DecideToMakeTrade(std::vector<Order> proposedOrders)
{
    string orderType;
    for (auto& order : proposedOrders) 
    {
        if(orderType.size() == 0)
        {
            orderType = order.getTypeAsString();
        }
        else if(orderType!= order.getTypeAsString())
        {
            return false;
        }
    }

    return true;
}
