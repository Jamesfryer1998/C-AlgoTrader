#pragma once

#include <memory>
#include <vector>
#include <nlohmann/json.hpp>
#include "RiskEngine.hpp"

using json = nlohmann::json;

/**
 * RiskEngineFactory
 *
 * Builds the risk engine from config/risk.json's "risk" block (FIXED_SIZE when there isn't one). bars_per_year from
 * outside the block is passed in too (VOLATILITY_TARGET needs it).
 */
namespace RiskEngineFactory
{
    // config: the app config (its "risk" block and bars_per_year)
    // Throws std::runtime_error for an unknown engine name
    std::unique_ptr<RiskEngine> create(const json& config);

    // Every engine's name, e.g. for --help
    std::vector<std::string> names();
}
