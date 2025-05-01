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
        bool isValidQuantity(const Order& order);
        bool isValidOrderType(const Order& order);
        bool isValidPrice(const Order& order, MarketData& marketData);
        bool checkStopLoss(const Order& order, MarketData& marketData);
        bool checkTickSize(const Order& order, MarketData& marketData);
        bool checkSlippage(const Order& order, MarketData& marketData);
        bool checkTakeProfit(const Order& order, MarketData& marketData);
        bool checkMaxPositionSize(const Order& order, float totalHeldQuantity);

    private:
        // Risk constraints
        double maxExposure;
        double maxPositionSize;
        double slippageTolerance;
};