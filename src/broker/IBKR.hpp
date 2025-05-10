#include "BrokerBase.hpp"
#include "EClientSocket.h"

#define MAX_CONNECTION_RETRY 5;

class IBKR : public BrokerBase 
{
    IBKR();
    ~IBKR();

    void SetUp();
    int connect();
    int disconnect();
    float getLatestPrice(std::string ticker);
    int placeOrder(oms::Order order);
    oms::Position getLatestPosition(std::string ticker);

    EClientSocket *m_pClient;


};
