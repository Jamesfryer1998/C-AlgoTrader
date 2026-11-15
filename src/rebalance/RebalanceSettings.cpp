#include "RebalanceSettings.hpp"

#include <algorithm>

namespace
{
    const std::string STOCKS = "stocks", LONG_ONLY = "stocks_long_only", SIGN = "sign";
    const std::string STEP = "Front-running the month-end rebalance";
}

RebalanceSettings
RebalanceSettings::fromConfig(const json& config, const std::string& yesterday)
{
    RebalanceSettings settings;
    settings.start = config.value("start", std::string());
    settings.developmentEnd = config.value("development_end", std::string());
    settings.validationEnd = config.value("validation_end", std::string());
    settings.end = config.value("end", std::string());
    if (settings.end.empty())
        settings.end = yesterday;
    settings.dataStart = config.value("data_start", settings.start);
    settings.stocks = config.value("stocks", settings.stocks);
    settings.adjustment = config.value("adjustment", settings.adjustment);
    settings.targetStockWeight = config.value("target_stock_weight", settings.targetStockWeight);
    settings.windowDays = config.value("window_days", settings.windowDays);
    settings.reversalDay = config.value("reversal_day", settings.reversalDay);
    settings.fullAtDrift = config.value("full_at_drift", settings.fullAtDrift);
    settings.startingCapital = config.value("starting_capital", settings.startingCapital);
    settings.randomDraws = config.value("random_draws", settings.randomDraws);
    settings.randomSeed = config.value("random_seed", settings.randomSeed);
    for (const auto& entry : config.value("variants", json::array()))
    {
        Variant variant;
        variant.name = entry.value("name", std::string());
        variant.bond = entry.value("bond", std::string());
        std::string legs = entry.value("legs", std::string());
        variant.legs = legs == STOCKS ? Legs::Stocks : legs == LONG_ONLY ? Legs::StocksLongOnly : Legs::Spread;
        variant.sizing = entry.value("sizing", std::string()) == SIGN ? Sizing::Sign : Sizing::Scaled;
        variant.slippagePercent = entry.value("slippage_percent", variant.slippagePercent);
        settings.variants.push_back(variant);
        if (std::find(settings.bonds.begin(), settings.bonds.end(), variant.bond) == settings.bonds.end())
            settings.bonds.push_back(variant.bond);
    }
    settings.gates = config.value("gates", json::object());
    return settings;
}

MorningSettings
RebalanceSettings::forReport() const
{
    json config = {{"start", start}, {"development_end", developmentEnd}, {"validation_end", validationEnd},
                   {"end", end}, {"starting_capital", startingCapital}, {"slippage_percent", 0.0},
                   {"selection", {{"stocks", 1 + bonds.size()}}}, {"gates", gates}};
    MorningSettings report = MorningSettings::fromConfig(config, end);
    for (const auto& variant : variants)
    {
        MorningSettings::Variant named;
        named.name = variant.name;
        named.step = STEP;
        report.variants.push_back(named);
    }
    return report;
}
