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

        void onNewOrder(Order& order);
        void inputMarketData(MarketData& inputData);

    private:

        void addStrategy(StrategyBase strat);

        OrderManagement oms;
        std::vector<MarketCondition> marketData;
        std::vector<StrategyBase> strategyList;
};  