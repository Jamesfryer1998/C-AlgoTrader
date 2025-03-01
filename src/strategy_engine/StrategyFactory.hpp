#include "../util/Config.hpp"


#define JSON_STRATEGY_CONFIG "/src/config/strategies.json"

class StrategyFactory
{
    public: 
        StrategyFactory(){};
        ~StrategyFactory(){};

        void loadJson(string filePath);
        void jsonToStrategy(json stratJson);
        void loadStrategies();
        void generatetrategies();

    private:

        string formatStrategyPath();
        string filePath;
        json strategyData;
        Config config;
};  