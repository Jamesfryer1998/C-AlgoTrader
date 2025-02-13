#include <iostream>
#include <nlohmann/json.hpp>

#define JSON_CONFIG_PATH "/Users/james/Projects/C++AlgoTrader/src/config/algo_trader.json"

using json = nlohmann::json;

// Check how we set up JSON CMAKE at work

class Config
{
    public:
        Config();
        ~Config();
        void loadJson(const std::string& filePath);
        json getJson(){ return configData; };

    private:
        json configData;
};