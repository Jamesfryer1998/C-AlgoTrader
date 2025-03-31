#include "BrokerBase.hpp"

class IBKR : public BrokerBase 
{
    IBKR();
    ~IBKR();

    int connect();
    int disconnect();
    float getLatestPrice(std::string ticker);
    int placeOrder(Order order);
    Position getLatestPosition(std::string ticker);

};
