#include <iostream>
#include "StrategyBase.hpp"
#include "../oms/OrderManagement.hpp"
#include "../data_access/MarketData.hpp"


class StrategyEngine
{
    public:
        StrategyEngine();
        ~StrategyEngine();

        void run();
        void setUp(json configData);

        void setMarketData(MarketData& inputData);

    private:

        void generateAndLoadStrategies();
        void executeStrategies();
        void printStategies();

        json configData;
        static OrderManagement* oms;
        std::vector<MarketCondition> marketData;
        std::vector<std::unique_ptr<StrategyBase>> strategyList;
};  