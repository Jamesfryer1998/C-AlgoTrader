#include <iostream>
#include "../data_access/MarketData.hpp"
#include "../oms/Order.hpp"
#include "../oms/Position.hpp"



class StrategyBase {
public:
    virtual ~StrategyBase() = default;
    virtual void execute() = 0;  // Pure virtual function

    // Factory method for dynamic creation
    static std::unique_ptr<StrategyBase> create(const std::string& name);

protected:
    // Register a strategy name with a factory function
    static void registerStrategy(const std::string& name, std::function<std::unique_ptr<StrategyBase>()> creator);
};

// class StrategyBase
// {
// public:
//     virtual ~StrategyBase() = default;
//     virtual void getName() = 0;

// protected:
//     std::string name;
//     std::vector<Order> orders;
//     std::vector<Position> positions;
//     double capital;
//     bool isLive;
//     // RiskManager riskManager;
// };
