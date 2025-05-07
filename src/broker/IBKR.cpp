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

}

int
IBKR::connect()
{
    std::cout<<"We are about to connect to IBKR broker"<< std::endl;

    std::cout<<"We are connected to IBKR broker"<< std::endl;
    return 1;
}

int
IBKR::disconnect()
{
    std::cout<<"We are disconnected from IBKR broker"<< std::endl;
    return 0;
}



float 
IBKR::getLatestPrice(std::string ticker)
{
    return 0;
}

int 
IBKR::placeOrder(Order order)
{
    return 0;
}

Position
IBKR::getLatestPosition(std::string ticker)
{
    return Position();
}
