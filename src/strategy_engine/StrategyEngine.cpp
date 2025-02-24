#include "StrategyEngine.hpp"

StrategyEngine::StrategyEngine()
{
}

StrategyEngine::~StrategyEngine()
{
}


void 
StrategyEngine::setUp()
{
    loadStrategies();

}

void
StrategyEngine::run()
{
    
}

void
StrategyEngine::inputMarketData(MarketData& inputData)
{
    marketData = inputData.getData();
}

void
StrategyEngine::onNewOrder(Order& order)
{
    oms.addOrder(order);
}

void
StrategyEngine::addStrategy(StrategyBase strat)
{
    strategyList.push_back(strat);
}

void
StrategyEngine::loadStrategies()
{
    Config config;
    string path = config.getProjectRoot();
    path += JSON_STRATEGY_CONFIG;
    config.loadJson(path);
    json stratJson = config.getJson();

    jsonToStrategy(stratJson);
}

void
StrategyEngine::jsonToStrategy(json json)
{
    int numStrats = json["strategy"].size();

    for (int i = 0; i < numStrats; ++i) 
    {
        json strat = json["strategy"][i];
    }
}