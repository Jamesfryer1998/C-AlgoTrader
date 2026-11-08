#include "StockLane.hpp"

#include <algorithm>
#include "../risk/RiskEngineFactory.hpp"

StockLane::StockLane(const PortfolioEntry& entry)
    : ticker(entry.ticker),
      config(entry.config),
      factory(std::make_unique<StrategyFactory>(config)),
      risk(RiskEngineFactory::create(config))
{
    engine.setUp(config, *factory, marketData);
    engine.setPrintStatus(false);   // the live app shows each strategy's levels in its table instead
    barsToKeep = static_cast<size_t>(std::max(engine.barsNeeded(), 1));
}

std::string
StockLane::strategyNames() const
{
    std::string names;
    for (const auto& strategy : activeStrategies())
        names += (names.empty() ? "" : ", ") + strategy.value("name", std::string("?"));
    return names;
}

json
StockLane::activeStrategies() const
{
    json active = json::array();
    for (const auto& strategy : config["strategies"])
        if (strategy.value("active", 0))
            active.push_back(strategy);
    return active;
}

void
StockLane::backfill(const std::vector<MarketCondition>& history)
{
    std::vector<MarketCondition> bars = history;
    marketData.update(bars);
    backfilled = bars.size();
}

void
StockLane::addBar(const MarketCondition& bar)
{
    marketData.addBar(bar, barsToKeep);
}
