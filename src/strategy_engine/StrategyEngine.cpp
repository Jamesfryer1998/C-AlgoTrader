#include <algorithm>
#include "StrategyEngine.hpp"
#include "StrategyFactory.hpp"
#include "../util/Log.hpp"


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
    Log::out() << "Setting up Strategy Engine..." << std::endl;
    configData = configdata;
    oms->setUp(configData, broker);
    strategyList = stratFactory.generateStrategies();
    marketData = &marketdata; // Store a pointer to the MarketData object

    Log::out() << "  --> Config Data set" << std::endl;
    Log::out() << "  --> OMS set" << std::endl;
    Log::out() << "  --> Broker set " << broker->brokerName << std::endl;
    Log::out() << "  --> Strategies generated" << std::endl;
    Log::out() << "  --> MarketData set\n" << std::endl;

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

    // What we hold in this ticker, so strategies can see trades the broker closed (stop loss / take profit)
    float held = 0;
    if (!marketData->getData().empty())
    {
        const std::string& ticker = marketData->getData().back().Ticker;
        for (const auto& position : oms->positions)
            if (position.getTicker() == ticker)
                held += position.getQuantity();
    }

    // 1. Ask every strategy for its view of this bar
    std::vector<Order> proposals;
    for (auto& strat : strategyList) 
    {
        strat->supplyData(*marketData);
        strat->setCurrentPosition(held);
        strat->execute();

        if (Log::verbose && printStatus)
        {
            std::string line = strat->status();
            if (line.empty())
                line = strat->_strategyAttribute.name + "   warming up: " + std::to_string(marketData->getData().size())
                     + " of " + std::to_string(strat->barsNeeded()) + " bars needed";
            Log::out() << line << std::endl;
        }

        if(strat->onNewOrder())
        {
            Order proposal = strat->getOrder();
            proposal.setStrategy(strat->_strategyAttribute.name);
            proposals.push_back(proposal);
        }
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
            Log::out() << "Strategies disagree (" << first.getTypeAsString() << " vs "
                      << proposal.getTypeAsString() << " on " << proposal.getTicker()
                      << "), no order sent" << std::endl;
            return false;
        }
    }

    if (proposals.size() > 1)
        Log::out() << proposals.size() << " strategies agree: " << first.getTypeAsString() << std::endl;

    chosen = first;

    // Record every strategy that agreed, e.g. "RSI+MACD"
    std::string names;
    for (const auto& proposal : proposals)
        if (!proposal.getStrategy().empty())
            names += (names.empty() ? "" : "+") + proposal.getStrategy();
    chosen.setStrategy(names);
    return true;
}

void
StrategyEngine::printStategies()
{
    Log::out() << "Loaded strategies:" << std::endl;

    for (auto& strat : strategyList) 
    {
        Log::out() << "  -> "  << strat->_strategyAttribute.name << std::endl;
    }
}

int
StrategyEngine::barsNeeded() const
{
    int most = 0;
    for (const auto& strat : strategyList)
        most = std::max(most, strat->barsNeeded());
    return most;
}

std::vector<StrategyBase::Levels>
StrategyEngine::levels() const
{
    std::vector<StrategyBase::Levels> out;
    for (const auto& strat : strategyList)
    {
        StrategyBase::Levels l = strat->levels();
        if (l.label.empty())
            l.label = strat->_strategyAttribute.name;
        if (!l.ready && marketData)
            l.state = "warming up: " + std::to_string(marketData->getData().size()) + "/" +
                      std::to_string(strat->barsNeeded()) + " bars";
        out.push_back(l);
    }
    return out;
}
