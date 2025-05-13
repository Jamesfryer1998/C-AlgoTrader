#include "BrokerBase.hpp"
#include "TestCppClient.h"
#include <unistd.h> 

#define MAX_CONNECTION_RETRY 5;
#define LIVE_TRADING_PORT 7496
#define PAPER_TRADING_PORT 7497

using IBKRClient = TestCppClient;

class IBKR : public BrokerBase
{
    public:
        IBKR();
        ~IBKR();

        void SetUp();

        void test();

    private:
        int connect(){ return 1;};
        int connected(const char* host, int port, int clientId);
        int disconnect();
        float getLatestPrice(std::string ticker);
        int placeOrder(oms::Order order);
        oms::Position getLatestPosition(std::string ticker);

        IBKRClient broker;


};
