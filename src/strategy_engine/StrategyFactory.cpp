#include <fstream>
#include "StrategyFactory.hpp"
#include "StrategyAttribute.hpp"
#include "RSI.hpp"
#include "MACD.hpp"
#include "MEANREV.hpp"

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

// Strat factory
// std::vector<std::unique_ptr<StrategyBase>> 
// StrategyFactory::generateStrategies() 
// {
    
//     std::vector<std::unique_ptr<StrategyBase>> strategies;
//     int numStrats = strategyData["strategies"].size();

//     for (int i = 0; i < numStrats; ++i) {
//         std::string name = strategyData["strategies"][i]["name"];
//         auto strategy = StrategyBase::create(name);
//         if (strategy) {
//             strategies.push_back(std::move(strategy));
//         } else {
//             std::cerr << "Failed to create strategy: " << name << std::endl;
//         }
//     }
//     return strategies;
// }


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

        if(name == "RSI")
            strategies.push_back(std::make_unique<RSI>(attributes));
        else if (name == "MACD")
            strategies.push_back(std::make_unique<MACD>(attributes));
        else if (name == "MEANREV")
            strategies.push_back(std::make_unique<MEANREV>(attributes));
    }
    return strategies;
}