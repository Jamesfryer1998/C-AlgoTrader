#include <fstream>
#include "StrategyFactory.hpp"
#include "StrategyAttribute.hpp"
#include "RSI.hpp"
#include "MACD.hpp"
#include "MEANREV.hpp"

// Parse Strategies into StratFactory
// StartBase will be the base for each strat
// Strat factory will form out json into strategies


// Default constructor - uses the Config class to load the consolidated config
StrategyFactory::StrategyFactory()
{
    // Use the default Config loading mechanism to get the consolidated config
    Config configLoader;
    json fullConfig = configLoader.loadConfig();
    
    // Extract just the strategies section
    if (fullConfig.contains("strategies")) {
        json strategiesSection;
        strategiesSection["strategies"] = fullConfig["strategies"];
        strategyData = strategiesSection;
    } else {
        // Log warning if no strategies section found
        std::cerr << "Warning: No 'strategies' section found in config file!" << std::endl;
        // Create empty strategies array to prevent issues
        strategyData["strategies"] = json::array();
    }
}

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

string
StrategyFactory::formatStrategyPath()
{
    // This is no longer used in the default constructor
    // but kept for backwards compatibility
    string projectRoot = config.getProjectRoot();
    string configPath = projectRoot + JSON_CONFIG_NAME;
    return configPath;
}

void
StrategyFactory::loadJsonData(const json& configData)
{
    // Extract strategies section from the provided config
    if (configData.contains("strategies")) {
        json strategiesSection;
        strategiesSection["strategies"] = configData["strategies"];
        strategyData = strategiesSection;
    } else {
        // Log warning if no strategies section found
        std::cerr << "Warning: No 'strategies' section found in provided config!" << std::endl;
        // Create empty strategies array to prevent issues
        strategyData["strategies"] = json::array();
    }
}

void
StrategyFactory::loadStrategies()
{
    // Recreate the StrategyFactory to reload from default config
    *this = StrategyFactory();
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