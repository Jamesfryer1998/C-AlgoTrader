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

        // What happened to the most recent placeOrder() call. Each broker fills this in so the OMS
        // can save it alongside the trade without changing placeOrder's signature.
        struct OrderReceipt
        {
            std::string brokerOrderId;   // the broker's own id for the order
            std::string brokerStatus;    // e.g. "accepted", "new", "rejected"
            std::string orderClass;      // e.g. "simple", "bracket"
            std::string error;           // why the broker refused it, if it did
            double roundTripMs = 0;      // time to send the order and get the reply
        };
        OrderReceipt lastReceipt;

        // false if the last getLatestPosition() call failed (e.g. a network blip), so its answer
        // must not be trusted as "we hold nothing"
        bool lastPositionReliable = true;

        std::string brokerName;
};
