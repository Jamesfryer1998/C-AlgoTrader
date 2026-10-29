#pragma once

#include <iostream>
#include "../oms/Order.hpp"
#include "../oms/Position.hpp"


class BrokerBase
{
    public:
        BrokerBase(){};

        virtual ~BrokerBase() = default;
        virtual int connect() = 0;
        virtual int disconnect() = 0;
        virtual float getLatestPrice(std::string ticker) = 0;
        virtual int placeOrder(Order order) = 0;
        virtual Position getLatestPosition(std::string ticker) = 0;

        // False when the broker only pretends to trade (e.g. a dry run), so its positions
        // can't be compared with the orders we sent
        virtual bool reportsRealPositions() const { return true; }

        std::string brokerName;
};
