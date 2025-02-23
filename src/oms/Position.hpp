#include <iostream>

using namespace std;

class Position
{
    public:
        Position(){};
        Position(
            int _id,
            string _ticker,
            float _quantity,
            float _avgPrice);
        ~Position();

        int getId(){ return id;};

    private:
        int id;
        string ticker;
        float quantity;
        float avgPrice;
};