#include "BrokerBase.hpp"

#define MAX_CONNECTION_RETRY 5;

class IBKR : public BrokerBase 
{
    IBKR();
    ~IBKR();

    void SetUp();
    int connect();
    int disconnect();
    float getLatestPrice(std::string ticker);
    int placeOrder(Order order);
    Position getLatestPosition(std::string ticker);

};
