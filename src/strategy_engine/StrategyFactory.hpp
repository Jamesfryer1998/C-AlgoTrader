#pragma once

#include "../util/Config.hpp"
#include "StrategyBase.hpp"


// Using consolidated config file now

class StrategyFactory
{
    public: 
        StrategyFactory();
        StrategyFactory(string filePath);
        StrategyFactory(const json& configData);
        ~StrategyFactory(){};

        void loadJson(string filePath);
        void loadJsonData(const json& configData);
        std::vector<std::unique_ptr<StrategyBase>> generateStrategies();
        json getJson(){ return strategyData;};

    private:

        string formatStrategyPath();
        json strategyData;
        Config config;
};  