#include "../oms/Order.hpp"
#include "StrategyBase.hpp"

class MACD : public StrategyBase {
    public:
        MACD(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) {}

        void execute() override 
        {
            validate();
            std::cout << "  -> MACD" << std::endl;
        }

        void validate() override
        {
            
            std::cout << "short_period" << _strategyAttribute.short_period<< std::endl;

            if(!_strategyAttribute.short_period)
                throw std::runtime_error("MACD short_period not set");
            if(!_strategyAttribute.long_period)
                throw std::runtime_error("MACD long_period not set");
            if(!_strategyAttribute.signal_period)
                throw std::runtime_error("MACD signal_period not set");
        }  

        StrategyAttribute getAttributes(){ return _strategyAttribute; }    
};