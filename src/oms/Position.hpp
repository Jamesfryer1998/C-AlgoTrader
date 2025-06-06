#pragma once
#include <iostream>


using namespace std;

class Position
{
    public:
        Position(){};
        Position(
            string _ticker,
            float _quantity,
            float _avgPrice);
        ~Position();

        // getters and setters
        int getId() const { return id;};
        string getTicker() const{ return ticker;}
        float getQuantity() const {return quantity;};
        float getAvgPrice() const {return avgPrice;};

        void setId(int _id) {id = _id;};
        void setTicker(string  _ticker) {ticker = _ticker;}
        void setQuantity(float _quantity) {quantity = _quantity;}
        void setAvgPrice(float _avgPrice) {avgPrice = _avgPrice;}
    
    private:
        int id;
        string ticker;
        float quantity;
        float avgPrice;
};