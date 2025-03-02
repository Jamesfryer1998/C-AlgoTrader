#pragma once

#include <iostream>
#include "StrategyAttribute.hpp"


// class StrategyBase {
// public:
//     virtual ~StrategyBase() = default;
//     virtual void execute() = 0;  // Pure virtual function
//     static std::unique_prt<StrategyBase> create(const std::string& name);

// protected:
    // Register a strategy name with a factory function
    // static void registerStrategy(const std::string& name, std::function<std::unique_ptr<StrategyBase>()> creator);

// };

class StrategyBase
{
public:
    StrategyBase(StrategyAttribute strategyAttribute):
       _strategyAttribute(strategyAttribute)
    {};
    virtual ~StrategyBase() = default;
    virtual void execute() = 0;

protected:
    StrategyAttribute _strategyAttribute;
};
