#include <iostream>

using namespace std;

class Order {
    public:
        Order(
            int _id,
            string _type, 
            string _ticker, 
            float _quantity, 
            float _price
        );
        ~Order();

    private:
        int id;
        string type;
        string ticker;
        float quantity;
        float price;
};
