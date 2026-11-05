#include <algorithm>
#include "StrategyEngine.hpp"
#include "StrategyFactory.hpp"
#include "../util/Log.hpp"


StrategyEngine::StrategyEngine()
{
}

StrategyEngine::~StrategyEngine()
{
    // We don't own the marketData pointer, so don't delete it
}


void 
StrategyEngine::setUp(json configdata, StrategyFactory &stratFactory, const MarketData &marketdata)
{    
    Log::out() << "Setting up Strategy Engine..." << std::endl;
    configData = configdata;
    strategyList = stratFactory.generateStrategies();
    marketData = &marketdata;
    printStategies();
}

std::optional<Order>
StrategyEngine::decide(float heldQuantity)
{
    if (marketData == nullptr || marketData->getData().empty())
        return std::nullopt;
    return agreedOrder(collectProposals(heldQuantity));
}

// Every strategy's view of the newest bar
std::vector<Order>
StrategyEngine::collectProposals(float heldQuantity)
{
    std::vector<Order> proposals;
    for (auto& strategy : strategyList)
    {
        std::optional<Order> proposal = strategy->onBar(*marketData, heldQuantity);
        printStatusLine(*strategy);
        if (proposal)
            proposals.push_back(*proposal);
    }
    return proposals;
}

void
StrategyEngine::printStatusLine(const StrategyBase& strategy) const
{
    if (!Log::verbose || !printStatus)
        return;
    std::string line = strategy.status();
    if (line.empty())
        line = strategy._strategyAttribute.name + "   warming up: " + std::to_string(marketData->getData().size())
             + " of " + std::to_string(strategy.barsNeeded()) + " bars needed";
    Log::out() << line << std::endl;
}

std::optional<Order>
StrategyEngine::agreedOrder(const std::vector<Order>& proposals)
{
    if (proposals.empty())
        return std::nullopt;

    const Order& first = proposals.front();
    for (const auto& proposal : proposals)
    {
        if (proposal.getTicker() != first.getTicker() || proposal.isBuy() != first.isBuy())
        {
            Log::out() << "Strategies disagree (" << first.getTypeAsString() << " vs "
                      << proposal.getTypeAsString() << " on " << proposal.getTicker()
                      << "), no order sent" << std::endl;
            return std::nullopt;
        }
    }

    if (proposals.size() > 1)
        Log::out() << proposals.size() << " strategies agree: " << first.getTypeAsString() << std::endl;

    // Record every strategy that agreed, e.g. "RSI+MACD"
    Order chosen = first;
    std::string names;
    for (const auto& proposal : proposals)
        if (!proposal.getStrategy().empty())
            names += (names.empty() ? "" : "+") + proposal.getStrategy();
    chosen.setStrategy(names);
    return chosen;
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

std::vector<StrategyBase::DecisionLevels>
StrategyEngine::decisionLevels() const
{
    std::vector<StrategyBase::DecisionLevels> out;
    for (const auto& strat : strategyList)
    {
        StrategyBase::DecisionLevels l = strat->decisionLevels();
        if (l.label.empty())
            l.label = strat->_strategyAttribute.name;
        if (!l.ready && marketData)
            l.state = "warming up: " + std::to_string(marketData->getData().size()) + "/" +
                      std::to_string(strat->barsNeeded()) + " bars";
        out.push_back(l);
    }
    return out;
}
