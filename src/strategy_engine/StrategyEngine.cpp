#include "StrategyEngine.hpp"
#include "StrategyFactory.hpp"


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
    
    // Strategies and the OMS all read the same MarketData object (no copies).
    // Live: main() adds each new bar to it. Backtest: the adapter updates it.
    oms->setMarketData(*marketData);
    
    // Execute all active strategies
    executeStrategies();
}

void
StrategyEngine::executeStrategies()
{   
    // Check if marketData pointer is valid
    if (marketData == nullptr) {
        std::cerr << "ERROR: marketData pointer is null in StrategyEngine::executeStrategies()" << std::endl;
        return;
    }

    // 1. Ask every strategy for its view of this bar
    std::vector<Order> proposals;
    for (auto& strat : strategyList) 
    {
        strat->supplyData(*marketData);
        strat->execute();

        if(strat->onNewOrder())
            proposals.push_back(strat->getOrder());
    }

    // 2. Only trade if the strategies that want to trade agree with each other
    Order order;
    if (chooseOrder(proposals, order))
        oms->onNewOrder(order);
}

bool
StrategyEngine::chooseOrder(const std::vector<Order>& proposals, Order& chosen)
{
    if (proposals.empty())
        return false;

    const Order& first = proposals.front();
    for (const auto& proposal : proposals)
    {
        if (proposal.getTicker() != first.getTicker() || proposal.isBuy() != first.isBuy())
        {
            std::cout << "Strategies disagree (" << first.getTypeAsString() << " vs "
                      << proposal.getTypeAsString() << " on " << proposal.getTicker()
                      << "), no order sent" << std::endl;
            return false;
        }
    }

    if (proposals.size() > 1)
        std::cout << proposals.size() << " strategies agree: " << first.getTypeAsString() << std::endl;

    chosen = first;
    return true;
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
