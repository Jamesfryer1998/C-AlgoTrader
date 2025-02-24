#include <iostream>
#include "../data_access/MarketData.hpp"
#include "../oms/Order.hpp"
#include "../oms/Position.hpp"

class StrategyBase
{
public:
    StrategyBase(std::string strategyName) : name(strategyName), isLive(false) {};
    ~StrategyBase() {};

    // Pure virtual methods - must be implemented by derived strategies
    // virtual void onMarketData(const MarketData& data) = 0;
    // virtual Order generateSignal() = 0;

    // Shared functionality
    void setLive(bool live) { isLive = live; }

protected:
    std::string name;
    std::vector<Order> orders;
    std::vector<Position> positions;
    double capital;
    bool isLive;
    // RiskManager riskManager;
};
