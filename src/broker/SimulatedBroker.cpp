#include "SimulatedBroker.hpp"

SimulatedBroker::SimulatedBroker()
{

}

SimulatedBroker::~SimulatedBroker()
{
    disconnect();
}

int
SimulatedBroker::connect()
{
    std::cout<<"We are connected to Simulated broker"<< std::endl;
    return 1;
}

int
SimulatedBroker::disconnect()
{
    std::cout<<"We are disconnected from Simulated broker"<< std::endl;
    return 1;
}

float 
SimulatedBroker::getLatestPrice(std::string ticker)
{
    return 0;
}

void 
SimulatedBroker::placeOrder(Order order)
{

}

void
SimulatedBroker::getLatestPosition(std::string ticker)
{
    return;
}

Position 
SimulatedBroker::returnPosition(std::string ticker)
{
    return Position();
}