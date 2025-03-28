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
        virtual void placeOrder(Order order) = 0;
        virtual void getLatestPosition(std::string ticker) = 0;
        virtual Position returnPosition(std::string ticker) = 0;

    private:

        bool connected;

};
