#include <fstream>
#include "StrategyFactory.hpp"

// Parse Strategies into StratFactory
// StartBase will be the base for each strat
// Strat factory will form out json into strategies


// Default constructor
StrategyFactory::StrategyFactory()
{
    loadJson(formatStrategyPath());
}

// Constructor for testing
StrategyFactory::StrategyFactory(string filePath)
{
    loadJson(filePath);
}

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

std::vector<std::unique_ptr<StrategyBase>> 
StrategyFactory::generateStrategies() 
{
    std::vector<std::unique_ptr<StrategyBase>> strategies;
    int numStrats = strategyData["strategies"].size();

    for (int i = 0; i < numStrats; ++i) {
        std::string name = strategyData["strategies"][i]["name"];
        auto strategy = StrategyBase::create(name);
        if (strategy) {
            strategies.push_back(std::move(strategy));
        } else {
            std::cerr << "Failed to create strategy: " << name << std::endl;
        }
    }
    return strategies;
}