#include "StrategyBase.hpp"

class RSI : public StrategyBase {
public:
    RSI(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) {}

    void execute() override {
        std::cout << "Executing RSI" << std::endl;
    }

    StrategyAttribute getAttributes() { return _strategyAttribute; }
};
