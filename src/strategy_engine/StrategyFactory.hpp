#pragma once

#include "../util/Config.hpp"
#include "StrategyBase.hpp"


#define JSON_STRATEGY_CONFIG "/src/config/strategies.json"

class StrategyFactory
{
    public: 
        StrategyFactory();
        StrategyFactory(string filePath);
        ~StrategyFactory(){};

        void loadJson(string filePath);
        void loadStrategies();
        std::vector<std::unique_ptr<StrategyBase>> generateStrategies();
        json getJson(){ return strategyData;};

    private:

        string formatStrategyPath();
        json strategyData;
        Config config;
};  