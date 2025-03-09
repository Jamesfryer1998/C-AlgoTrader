#include "StrategyBase.hpp"

class MACD : public StrategyBase {
    public:
        MACD(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) {}

        void execute() override {
            std::cout << "Executing MACD" << std::endl;
        }

        void validate() override
        {

        }

        StrategyAttribute getAttributes(){ return _strategyAttribute;}
    
};