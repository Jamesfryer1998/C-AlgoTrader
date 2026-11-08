#include <fstream>
#include "StrategyFactory.hpp"
#include "StrategyAttribute.hpp"
#include "RSI.hpp"
#include "MACD.hpp"
#include "MEANREV.hpp"
#include "SCALP.hpp"

// Parse Strategies into StratFactory
// StartBase will be the base for each strat
// Strat factory will form out json into strategies


// Constructor for testing - accepts a custom file path
StrategyFactory::StrategyFactory(string filePath)
{
    loadJson(filePath);
}

// Constructor with direct JSON data
StrategyFactory::StrategyFactory(const json& configData)
{
    loadJsonData(configData);
}

void
StrategyFactory::loadJson(string filePath)
{
    // For the test case with a specific strategies file
    config.loadJson(filePath);
    strategyData = config.getJson();
}

void
StrategyFactory::loadJsonData(const json& configData)
{
    if (configData.contains("strategies")) {
        json strategiesSection;
        strategiesSection["strategies"] = configData["strategies"];
        strategyData = strategiesSection;
    } else {
        std::cerr << "Warning: No 'strategies' section found in provided config!" << std::endl;
        strategyData["strategies"] = json::array();
    }
}

std::vector<std::unique_ptr<StrategyBase>> 
StrategyFactory::generateStrategies() 
{   
    std::vector<std::unique_ptr<StrategyBase>> strategies;
    int numStrats = strategyData["strategies"].size();

    for (int i = 0; i < numStrats; ++i) {
        std::string name = strategyData["strategies"][i]["name"];
        int active = strategyData["strategies"][i]["active"].get<int>();
        StrategyAttribute attributes(strategyData["strategies"][i]);

        // Leave if strat not active
        if(!active)
            continue;

        std::unique_ptr<StrategyBase> strategy;
        if(name == RSI::NAME)
            strategy = std::make_unique<RSI>(attributes);
        else if (name == MACD::NAME)
            strategy = std::make_unique<MACD>(attributes);
        else if (name == MEANREV::NAME)
            strategy = std::make_unique<MEANREV>(attributes);
        else if (name == SCALP::NAME)
            strategy = std::make_unique<SCALP>(attributes);
        else
            throw std::runtime_error("Unknown strategy in config: " + name);

        // Check the settings once here, so a bad config fails at start-up rather than on every bar
        strategy->validate();
        strategies.push_back(std::move(strategy));
    }
    return strategies;
}