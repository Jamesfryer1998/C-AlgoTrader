#include "GridSettings.hpp"

namespace
{
    const std::string FUNDS = "funds";
    const std::string STEP = "Buying drops, selling pops";
}

GridSettings
GridSettings::fromConfig(const json& config, const std::string& yesterday)
{
    GridSettings settings;
    settings.start = config.value("start", std::string());
    settings.developmentEnd = config.value("development_end", std::string());
    settings.validationEnd = config.value("validation_end", std::string());
    settings.end = config.value("end", std::string());
    if (settings.end.empty())
        settings.end = yesterday;
    settings.dataStart = config.value("data_start", settings.start);
    json universe = config.value("universe", json::object());
    settings.stocks = universe.value("stocks", settings.stocks);
    settings.formationDays = universe.value("formation_days", settings.formationDays);
    settings.minPrice = universe.value("min_price", settings.minPrice);
    settings.prefilterDollarVolume = universe.value("prefilter_dollar_volume", settings.prefilterDollarVolume);
    settings.loadDollarVolume = universe.value("load_dollar_volume", settings.loadDollarVolume);
    settings.funds = config.value("funds", std::vector<std::string>());
    settings.notStocks = config.value("not_stocks", std::vector<std::string>());
    settings.startInvested = config.value("start_invested", settings.startInvested);
    settings.slippagePercent = config.value("slippage_percent", settings.slippagePercent);
    settings.startingCapital = config.value("starting_capital", settings.startingCapital);
    settings.randomSeed = config.value("random_seed", settings.randomSeed);
    for (const auto& entry : config.value("variants", json::array()))
    {
        Variant variant;
        variant.name = entry.value("name", std::string());
        variant.universe = entry.value("universe", std::string()) == FUNDS ? Universe::Funds : Universe::Stocks;
        variant.triggerPercent = entry.value("trigger_percent", variant.triggerPercent);
        variant.stepPercent = entry.value("step_percent", variant.stepPercent);
        variant.mirror = entry.value("mirror", variant.mirror);
        variant.atClose = entry.value("at_close", variant.atClose);
        settings.variants.push_back(variant);
    }
    settings.gates = config.value("gates", json::object());
    return settings;
}

MorningSettings
GridSettings::forReport() const
{
    json config = {{"start", start}, {"development_end", developmentEnd}, {"validation_end", validationEnd},
                   {"end", end}, {"starting_capital", startingCapital}, {"slippage_percent", slippagePercent},
                   {"selection", {{"stocks", stocks}}}, {"gates", gates}};
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
