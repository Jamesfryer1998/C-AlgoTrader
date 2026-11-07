#include "RiskEngineFactory.hpp"

#include <stdexcept>
#include "FixedFractionRisk.hpp"
#include "FixedSizeRisk.hpp"
#include "RiskPerTradeRisk.hpp"
#include "VolatilityTargetRisk.hpp"

namespace
{
    const std::string RISK = "risk";

    // Keys from config/risk.json that apply to every engine, unless the "risk" block sets them itself
    const std::vector<std::string> SHARED_KEYS = {"bars_per_year"};
}

namespace RiskEngineFactory
{
    std::unique_ptr<RiskEngine> create(const json& config)
    {
        json settings = config.value(RISK, json::object());
        for (const auto& key : SHARED_KEYS)
            if (!settings.contains(key) && config.contains(key))
                settings[key] = config[key];

        std::string name = settings.value("name", std::string(FixedSizeRisk::NAME));
        if (name == FixedSizeRisk::NAME)
            return std::make_unique<FixedSizeRisk>(settings);   // capped by the OMS's max_position_size
        if (name == FixedFractionRisk::NAME)
            return std::make_unique<FixedFractionRisk>(settings);
        if (name == RiskPerTradeRisk::NAME)
            return std::make_unique<RiskPerTradeRisk>(settings);
        if (name == VolatilityTargetRisk::NAME)
            return std::make_unique<VolatilityTargetRisk>(settings);
        throw std::runtime_error("Unknown risk engine \"" + name + "\" (use one of: FIXED_SIZE, FIXED_FRACTION, "
                                 "RISK_PER_TRADE, VOLATILITY_TARGET)");
    }

    std::vector<std::string> names()
    {
        return {FixedSizeRisk::NAME, FixedFractionRisk::NAME, RiskPerTradeRisk::NAME, VolatilityTargetRisk::NAME};
    }
}
