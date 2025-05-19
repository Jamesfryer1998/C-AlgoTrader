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
        virtual double getLatestPrice(std::string ticker) = 0;
        virtual int placeOrder(oms::Order order) = 0;
        virtual oms::Position getLatestPosition(std::string ticker) = 0;

        std::string brokerName;

    private:
        bool connected;

};
