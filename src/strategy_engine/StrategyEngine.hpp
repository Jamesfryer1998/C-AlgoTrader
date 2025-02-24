#include <iostream>
#include "StrategyBase.hpp"
#include "../oms/OrderManagement.hpp"
#include "../data_access/MarketData.hpp"

#define JSON_STRATEGY_CONFIG "/src/config/strategies.json"

class StrategyEngine
{
    public:
        StrategyEngine();
        ~StrategyEngine();

        void run();
        void setUp();

        void onNewOrder(Order& order);
        void inputMarketData(MarketData& inputData);

    private:

        void addStrategy(StrategyBase strat);
        void jsonToStrategy(json json);
        void loadStrategies();

        OrderManagement oms;
        std::vector<MarketCondition> marketData;
        std::vector<StrategyBase> strategyList;
};  