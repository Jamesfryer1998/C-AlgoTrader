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
        void setUp();

        void inputMarketData(MarketData& inputData);

    private:

        void generateAndLoadStrategies();
        void executeStrategies();
        void printStategies();

        OrderManagement* oms;
        std::vector<MarketCondition> marketData;
        std::vector<std::unique_ptr<StrategyBase>> strategyList;
};  