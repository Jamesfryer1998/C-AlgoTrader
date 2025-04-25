#pragma once

#include <iostream>

#include "Order.hpp"
#include "Position.hpp"
#include "../data_access/MarketData.hpp"

class OrderValidator
{
    public:
        OrderValidator(){};

        bool validateOrder(const Order& order, MarketData& marketData, std::vector<Position>& positions);

        void setParams(json configData);
        float getTotalHeldQuantity(const Order& order, std::vector<Position>& positions);

        // Validation methods
        bool isValidOrderType(const Order& order);
        bool isValidPrice(const Order& order, MarketData& marketData);
        bool isValidQuantity(const Order& order);
        bool checkMaxPositionSize(const Order& order, float totalHeldQuantity);
        bool checkStopLoss(const Order& order, MarketData& marketData);
        bool checkTakeProfit(const Order& order, MarketData& marketData);
        bool checkTickSize(const Order& order, MarketData& marketData);
        bool checkSlippage(const Order& order, MarketData& marketData);

    private:
        // Risk constraints
        double maxPositionSize;
        double maxExposure;
        double slippageTolerance;
};