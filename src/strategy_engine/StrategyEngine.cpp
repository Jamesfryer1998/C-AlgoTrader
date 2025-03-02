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
StrategyEngine::addStrategy(std::unique_ptr<StrategyBase> strat)
{
    // strategyList.push_back(strat);
}