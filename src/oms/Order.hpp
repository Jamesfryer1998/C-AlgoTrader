#pragma once
#include <iostream>
#include <string>
#include <stdexcept>

using namespace std;

// Define order types as an enum for type safety
enum class OrderType {
    BUY,
    SELL,
    LIMIT_BUY,
    LIMIT_SELL,
    STOP_BUY,
    STOP_SELL,
    HOLD,
    UNKNOWN
};

// Helper functions for OrderType conversion
string orderTypeToString(OrderType type);
OrderType stringToOrderType(const string& typeStr);

class Order {
    public:
        Order(){};
        Order(
            // int _id,
            OrderType _type, 
            string _ticker, 
            float _quantity, 
            float _price
        );
        
        // For backwards compatibility
        Order(
            // int _id,
            string _typeStr, 
            string _ticker, 
            float _quantity, 
            float _price
        );
        
        ~Order();

        // getters and setters
        int getId() const { return id;};
        float getPrice() const {return price;}
        OrderType getType() const {return type;}
        string getTicker() const{ return ticker;}
        float getQuantity() const {return quantity;};
        float getStopLossPrice() const {return stopLossPrice;}
        float getTakeProfitPrice() const {return takeProfitPrice;}
        string getTypeAsString() const {return orderTypeToString(type);}
        string getStrategy() const { return strategy; }
        string getClientOrderId() const { return clientOrderId; }

        void setId(int _id) {id = _id;};
        void setStopLoss(float stopLossPercentage);
        void setPrice(float _price){price = _price;}
        void setType(OrderType _type){type = _type;}
        void setTakeProfit(float takeProfitPercentage);
        void setTicker(string  _ticker) {ticker = _ticker;}
        void setQuantity(float _quantity) {quantity = _quantity;}
        void setType(string _typeStr){type = stringToOrderType(_typeStr);}
        void setStrategy(const string& name) { strategy = name; }
        void setClientOrderId(const string& id) { clientOrderId = id; } // our id as the broker sees it

        // Set by the OMS: the position held before this order (+ long, - short). An order on the other
        // side of it reduces or closes the position, which some brokers handle differently from an entry
        // (Alpaca won't take a bracket order for it).
        void setPositionBefore(float quantity) { positionBefore = quantity; }
        float getPositionBefore() const { return positionBefore; }
        bool reducesPosition() const
        {
            return (positionBefore > 0 && isSell()) || (positionBefore < 0 && isBuy());
        }
        
        // Helper methods
        bool isMarket() const { return type == OrderType::BUY || type == OrderType::SELL; }
        bool isStop() const { return type == OrderType::STOP_BUY || type == OrderType::STOP_SELL; }
        bool isLimit() const { return type == OrderType::LIMIT_BUY || type == OrderType::LIMIT_SELL; }
        bool isBuy() const { return type == OrderType::BUY || type == OrderType::LIMIT_BUY || type == OrderType::STOP_BUY; }
        bool isSell() const { return type == OrderType::SELL || type == OrderType::LIMIT_SELL || type == OrderType::STOP_SELL; }

    private:
        int id;
        OrderType type;
        float positionBefore = 0;
        string ticker;
        float quantity;
        float price;
        float stopLossPrice;
        float takeProfitPrice;
        string strategy;       // e.g. "RSI", or "RSI+MACD" when several agreed
        string clientOrderId;  // sent to the broker so its order can be matched to ours
};
