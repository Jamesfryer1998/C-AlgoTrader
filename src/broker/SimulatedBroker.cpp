#include "SimulatedBroker.hpp"

SimulatedBroker::SimulatedBroker(MarketData marketdata)
: marketData(marketdata)
{
    brokerName = "Simulated";
    connect();
    step = 30;
}

SimulatedBroker::~SimulatedBroker()
{
    disconnect();
}

void
SimulatedBroker::process()
{
    if(marketData.getData().size() == 0)
    {
        throw std::runtime_error("No market data found");
        return;
    }

    currentCondition = marketData.getData()[step];
    simulationTime = currentCondition.DateTime;
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
    return currentCondition.Close;
}

void 
SimulatedBroker::placeOrder(Order order)
{
    float latestPrice = getLatestPrice(order.getTicker());
    orders.push_back(order);

    Position position{
        order.getTicker(),
        order.getQuantity(),
        latestPrice
    };

    positions.push_back(position);
}

Position
SimulatedBroker::getLatestPosition(std::string ticker)
{
    return positions.back();
}

void SimulatedBroker::nextStep()
{
    process();
    
    if (static_cast<size_t>(step+1) >= marketData.getData().size()) {
        std::cout << "Simulation finished." << std::endl;
        return;
    }

    // Move to the next market condition
    step++;
    currentCondition = marketData.getData()[step];

    // Advance the simulation time to match market data
    simulationTime = currentCondition.DateTime;
}