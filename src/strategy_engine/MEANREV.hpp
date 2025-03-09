#include "StrategyBase.hpp"

class MEANREV : public StrategyBase {
    public:
        MEANREV(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) {}

        void execute() override {
            std::cout << "Executing MEANREV" << std::endl;
        }


        void validate() override
        {
            
        }

        StrategyAttribute getAttributes(){ return _strategyAttribute;}
    
};