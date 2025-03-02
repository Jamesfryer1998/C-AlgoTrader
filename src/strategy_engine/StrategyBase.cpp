// #include "StrategyBase.hpp"

// #include <map>

// std::unordered_map<std::string, std::function<std::unique_ptr<StrategyBase>()>>& getStrategyRegistry() {
//     static std::unordered_map<std::string, std::function<std::unique_ptr<StrategyBase>()>> strategyRegistry;
//     return strategyRegistry;
// }

// void StrategyBase::registerStrategy(const std::string& name, std::function<std::unique_ptr<StrategyBase>()> creator) {
//     getStrategyRegistry()[name] = std::move(creator);
// }

// std::unique_ptr<StrategyBase> StrategyBase::create(const std::string& name) {
//     auto& registry = getStrategyRegistry();
//     auto it = registry.find(name);
//     if (it != registry.end()) {
//         return it->second();
//     }
//     std::cerr << "Unknown strategy type: " << name << std::endl;
//     return nullptr;
// }
