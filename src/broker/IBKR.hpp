#include "BrokerBase.hpp"

class IBKR : public BrokerBase 
{
    IBKR();
    ~IBKR();

    int connect();
    int disconnect();
    float getLatestPrice(std::string ticker);
    void placeOrder(Order order);
    Position getLatestPosition(std::string ticker);

};
