#pragma once

#include <iostream>

#include "Order.hpp"
#include "Position.hpp"
#include "../data_access/MarketData.hpp"

class OrderValidator
{
    public:
        OrderValidator(){};

        bool validateOrder(const oms::Order& order, MarketData& marketData, std::vector<oms::Position>& positions);

        void setParams(json configData);
        float getTotalHeldQuantity(const oms::Order& order, std::vector<oms::Position>& positions);

        // Validation methods
        bool isValidQuantity(const oms::Order& order);
        bool isValidOrderType(const oms::Order& order);
        bool isValidPrice(const oms::Order& order, MarketData& marketData);
        bool checkStopLoss(const oms::Order& order, MarketData& marketData);
        bool checkTickSize(const oms::Order& order, MarketData& marketData);
        bool checkSlippage(const oms::Order& order, MarketData& marketData);
        bool checkTakeProfit(const oms::Order& order, MarketData& marketData);
        bool checkMaxPositionSize(const oms::Order& order, float totalHeldQuantity);

    private:
        // Risk constraints
        double maxExposure;
        double maxPositionSize;
        double slippageTolerance;
};