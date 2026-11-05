#include "StockLane.hpp"

#include <algorithm>
#include <chrono>

StockLane::StockLane(const PortfolioEntry& entry, OrderManagement& sharedOms, BrokerBase& broker)
    : ticker(entry.ticker),
      config(entry.config),
      factory(std::make_unique<StrategyFactory>(config))
{
    engine.shareOrderManagement(&sharedOms);
    engine.setUp(config, *factory, marketData, &broker);
    engine.setPrintStatus(false);   // the live app shows each strategy's levels in its table instead
    barsToKeep = static_cast<size_t>(std::max(engine.barsNeeded(), 1));
}

std::string
StockLane::strategyNames() const
{
    std::string names;
    for (const auto& strategy : config["strategies"])
        if (strategy.value("active", 0))
            names += (names.empty() ? "" : ", ") + strategy.value("name", std::string("?"));
    return names;
}

void
StockLane::backfill(const std::vector<MarketCondition>& history)
{
    std::vector<MarketCondition> bars = history;
    marketData.update(bars);
}

double
StockLane::decide(const MarketCondition& bar)
{
    auto started = std::chrono::steady_clock::now();
    marketData.addBar(bar, barsToKeep);
    engine.run();   // points the shared OMS at this stock's prices, then decides
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}
