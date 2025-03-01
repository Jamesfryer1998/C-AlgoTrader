#include <fstream>
#include "StrategyFactory.hpp"

// Parse Strategies into StratFactory
// StartBase will be the base for each strat
// Strat factory will form out json into strategies
void
StrategyFactory::loadJson(string filePath)
{
    config.loadJson(filePath);
    strategyData = config.getJson();
}

string
StrategyFactory::formatStrategyPath()
{
    string projectRoot = config.getProjectRoot();
    string configPath = projectRoot + JSON_STRATEGY_CONFIG;
    return configPath;
}

void
StrategyFactory::loadStrategies()
{
    loadJson(formatStrategyPath());
}

void
StrategyFactory::generatetrategies()
{
    int numStrats = strategyData["strategy"].size();

    for (int i = 0; i < numStrats; ++i) 
    {
        json strat = strategyData["strategy"][i];
        std::cout << strat << std::endl;
    }
}