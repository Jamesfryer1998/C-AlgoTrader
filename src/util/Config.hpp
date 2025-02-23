#pragma once
#include <iostream>
#include <nlohmann/json.hpp>

#define JSON_CONFIG_NAME "/src/config/algo_trader.json"

using json = nlohmann::json;
using namespace std;

// Check how we set up JSON CMAKE at work

class Config
{
    public:
        Config();
        ~Config();
        void loadJson(const std::string& filePath);
        json loadConfig();
        json getJson(){ return configData; };
        string getProjectRoot();
        string formConfigPath();

    private:
        json configData;
};