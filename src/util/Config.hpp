#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// The settings files, under the project root. general.json, strategy.json and risk.json are read together
// as one config (each top-level key in only one of them); screen.json and optimisation.json are the
// research apps' own settings.
namespace ConfigFiles
{
    const std::string FOLDER = "config/";
    const std::string GENERAL = FOLDER + "general.json";     // data folder, capital, Alpaca
    const std::string STRATEGY = FOLDER + "strategy.json";   // the portfolio and each stock's strategies
    const std::string RISK = FOLDER + "risk.json";           // position sizing and order limits
    const std::string SCREEN = FOLDER + "screen.json";
    const std::string OPTIMISATION = FOLDER + "optimisation.json";

    const std::vector<std::string> APP_CONFIG = {GENERAL, STRATEGY, RISK};
}

using json = nlohmann::json;
using namespace std;

// Check how we set up JSON CMAKE at work

class Config
{
    public:
        Config();
        ~Config();
        
        // Load JSON from a file path (can be absolute or relative)
        void loadJson(const std::string& filePath);
        
        // Load general.json, strategy.json and risk.json as one config
        json loadConfig();
        
        // Get the loaded JSON data
        json getJson(){ return configData; };
        
        // Get the project root directory
        string getProjectRoot();
        
        // The full path of one of the ConfigFiles, e.g. formConfigPath(ConfigFiles::RISK)
        string formConfigPath(const string& configFile);
        
        // Get a path relative to the tests directory
        // Example: "test_data/config_test.json" -> "/full/path/to/projectroot/tests/test_data/config_test.json"
        string getTestPath(const std::string& relativeTestPath);
        
        // Debug method to print information about file paths
        // Useful for troubleshooting when tests can't find files
        void debugPaths(const std::string& testPath);

    private:
        json configData;
        bool loaded = false;
};