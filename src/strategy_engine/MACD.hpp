#include "../oms/Order.hpp"
#include "StrategyBase.hpp"

class MACD : public StrategyBase {
    public:
        MACD(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) {}

        void execute() override 
        {
            std::cout << "Executing MACD" << std::endl;
            validate();
        }

        void validate() override
        {
            if(!_strategyAttribute.short_period)
                throw std::runtime_error("RSI short_period not set");
            if(!_strategyAttribute.long_period)
                throw std::runtime_error("RSI long_period not set");
            if(!_strategyAttribute.signal_period)
                throw std::runtime_error("RSI signal_period not set");
        }  

        StrategyAttribute getAttributes(){ return _strategyAttribute; }    
};