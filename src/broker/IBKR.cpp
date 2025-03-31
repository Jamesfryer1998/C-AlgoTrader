
#include "IBKR.hpp"

IBKR::IBKR()
{

}

IBKR::~IBKR()
{
    disconnect();
}

int
IBKR::connect()
{
    std::cout<<"We are about to connect to IBKR broker"<< std::endl;



    std::cout<<"We are connected to IBKR broker"<< std::endl;
    return 0;
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
