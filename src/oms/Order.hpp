#pragma once
#include <iostream>

using namespace std;

class Order {
    public:
        Order(){};
        Order(
            // int _id,
            string _type, 
            string _ticker, 
            float _quantity, 
            float _price
        );
        ~Order();

        // getters and setters
        int getId() const { return id;};
        string getType() const {return type;}
        string getTicker() const{ return ticker;}
        float getQuantity() const {return quantity;};
        float getPrice() const {return price;}
        float getStopLossPrice() const {return stopLossPrice;}
        float getTakeProfitPrice() const {return takeProfitPrice;}

        void setId(int _id) {id = _id;};
        void setType(string _type){type = _type;}
        void setTicker(string  _ticker) {ticker = _ticker;}
        void setQuantity(float _quantity) {quantity = _quantity;}
        void setPrice(float _price){price = _price;}
        void setStopLoss(float stopLossPercentage);
        void setTakeProfit(float takeProfitPercentage);

    private:
        int id;
        string type;
        string ticker;
        float quantity;
        float price;
        float stopLossPrice;
        float takeProfitPrice;
};
