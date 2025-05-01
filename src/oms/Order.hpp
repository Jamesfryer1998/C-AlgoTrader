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

        void setId(int _id) {id = _id;};
        void setStopLoss(float stopLossPercentage);
        void setPrice(float _price){price = _price;}
        void setType(OrderType _type){type = _type;}
        void setTakeProfit(float takeProfitPercentage);
        void setTicker(string  _ticker) {ticker = _ticker;}
        void setQuantity(float _quantity) {quantity = _quantity;}
        void setType(string _typeStr){type = stringToOrderType(_typeStr);}
        
        // Helper methods
        bool isMarket() const { return type == OrderType::BUY || type == OrderType::SELL; }
        bool isStop() const { return type == OrderType::STOP_BUY || type == OrderType::STOP_SELL; }
        bool isLimit() const { return type == OrderType::LIMIT_BUY || type == OrderType::LIMIT_SELL; }
        bool isBuy() const { return type == OrderType::BUY || type == OrderType::LIMIT_BUY || type == OrderType::STOP_BUY; }
        bool isSell() const { return type == OrderType::SELL || type == OrderType::LIMIT_SELL || type == OrderType::STOP_SELL; }

    private:
        int id;
        OrderType type;
        string ticker;
        float quantity;
        float price;
        float stopLossPrice;
        float takeProfitPrice;
};
