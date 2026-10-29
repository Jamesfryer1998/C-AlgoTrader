#include "StrategyBase.hpp"

class MEANREV : public StrategyBase {
    public:
        MEANREV(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) {}

        void execute() override 
        {
            std::cout << "  -> MEANREV" << std::endl;
        }

        void validate() override
        {
            if(!_strategyAttribute.lookback_period)
                throw std::runtime_error("MEANREV lookback_period not set");
            if(!_strategyAttribute.entry_threshold)
                throw std::runtime_error("MEANREV entry_threshold not set");
            if(!_strategyAttribute.exit_threshold)
                throw std::runtime_error("MEANREV exit_threshold not set");
            if(!_strategyAttribute.moving_average_period)
                throw std::runtime_error("MEANREV moving_average_period not set");
        }
};