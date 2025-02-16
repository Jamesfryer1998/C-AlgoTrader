#include <iostream>

using namespace std;

class Position
{
    public:
        Position(
            string _ticker,
            float _quantity,
            float _avgPrice);
        ~Position();

    private:
        string ticker;
        float quantity;
        float avgPrice;
};