#include "StrategyBase.hpp"

class RSI : public StrategyBase {
    public:
        RSI(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) 
        {
            
        }
        
        void execute() override 
        {
            validate();
            std::cout << "Executing RSI" << std::endl;
        }

        void validate() override
        {
            if(!_strategyAttribute.period)
                throw std::runtime_error("RSI period not set");
            if(!_strategyAttribute.overbought_threshold)
                throw std::runtime_error("RSI overbought threshold not set");
            if(!_strategyAttribute.oversold_threshold)
                throw std::runtime_error("RSI oversold threshold not set");
        }  

        StrategyAttribute getAttributes() { return _strategyAttribute; }
};
