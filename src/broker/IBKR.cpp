#include "IBKR.hpp"


IBKR::IBKR()
{

}

IBKR::~IBKR()
{
    disconnect();
}

void
IBKR::SetUp()
{
    // if(!connect())
        // std::runtime_error("Could not connect to IBKR broker after retries" + MAX_CONNECTION_RETRY);

    connected("127.0.0.1", PAPER_TRADING_PORT, 8809497);
}

int
IBKR::connected(const char* host, int port, int clientId)
{
    broker.connect("127.0.0.1", 7497, 8809497);
    broker.connectAck();
    sleep(100);
    return 0;
}


int
IBKR::disconnect()
{
    std::cout<<"We are disconnected from IBKR broker"<< std::endl;
    return 0;
}

void 
IBKR::test()
{
    broker.historicalDataRequests();
    broker.processMessages();
}



float 
IBKR::getLatestPrice(std::string ticker)
{
    return 0;
}

int 
IBKR::placeOrder(oms::Order order)
{
    return 0;
}

oms::Position
IBKR::getLatestPosition(std::string ticker)
{
    return oms::Position();
}
