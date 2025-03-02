#include "StrategyBase.hpp"

class RSIStrategy : public StrategyBase {
    public:
        void execute() override {
            std::cout << "Executing Concrete Strategy A" << std::endl;
        }

    private:
        static bool registered;
};

// Self-register the class when the file is included
bool RSIStrategy::registered = [] {
    StrategyBase::registerStrategy("RSI", []() { return std::make_unique<RSIStrategy>(); });
    return true;
}();