#pragma once

#include <iostream>
#include "StrategyBase.hpp"
#include "../oms/OrderManagement.hpp"
#include "../data_access/MarketData.hpp"
#include "StrategyFactory.hpp"
#include "../broker/BrokerBase.hpp"


class StrategyEngine
{
    public:
        StrategyEngine();
        ~StrategyEngine();

        void run();
        void setUp(json configData, StrategyFactory &stratFilePath, MarketData &marketdata, BrokerBase* broker, bool backtest=false);

        void setMarketData(MarketData& inputData);
        OrderManagement* getOms() { return oms;};

    private:

        void generateAndLoadStrategies(string filePath);
        void executeStrategies();
        void printStategies();

        bool backtest;
        json configData;
        MarketData marketData;
        static OrderManagement* oms;
        std::vector<MarketCondition> marketConditions;
        std::vector<std::unique_ptr<StrategyBase>> strategyList;
};  