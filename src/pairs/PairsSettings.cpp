#include "PairsSettings.hpp"

namespace
{
    const std::string LEAD_LAG = "lead_lag";
}

PairsSettings
PairsSettings::fromConfig(const json& config, const std::string& yesterday)
{
    PairsSettings settings;
    settings.start = config.value("start", std::string());
    settings.developmentEnd = config.value("development_end", std::string());
    settings.validationEnd = config.value("validation_end", std::string());
    settings.end = config.value("end", std::string());
    if (settings.end.empty())
        settings.end = yesterday;
    settings.dataStart = config.value("data_start", settings.start);

    json universe = config.value("universe", json::object());
    settings.universeSize = universe.value("universe_size", settings.universeSize);
    settings.minPrice = universe.value("min_price", settings.minPrice);
    settings.minDollarVolume = universe.value("min_dollar_volume", settings.minDollarVolume);
    settings.prefilterDollarVolume = universe.value("prefilter_dollar_volume", settings.prefilterDollarVolume);

    settings.formationDays = config.value("formation_days", settings.formationDays);
    json pairs = config.value("pairs", json::object());
    settings.pairs = pairs.value("pairs", settings.pairs);
    settings.tradingDays = pairs.value("trading_days", settings.tradingDays);
    settings.openSigma = pairs.value("open_sigma", settings.openSigma);
    json leadLag = config.value("lead_lag", json::object());
    settings.stocksEachSide = leadLag.value("stocks_each_side", settings.stocksEachSide);
    settings.minCorrelation = leadLag.value("min_correlation", settings.minCorrelation);

    settings.startingCapital = config.value("starting_capital", settings.startingCapital);
    settings.slippagePercent = config.value("slippage_percent", settings.slippagePercent);
    settings.leverage = config.value("leverage", settings.leverage);
    settings.randomSeed = config.value("random_seed", settings.randomSeed);

    for (const auto& entry : config.value("variants", json::array()))
    {
        Variant variant;
        variant.name = entry.value("name", std::string());
        variant.kind = entry.value("kind", std::string()) == LEAD_LAG ? Kind::LeadLag : Kind::Pairs;
        variant.delayDays = entry.value("delay_days", variant.delayDays);
        variant.holdDays = entry.value("hold_days", variant.holdDays);
        settings.variants.push_back(variant);
    }
    settings.gates = config.value("gates", json::object());
    return settings;
}

MorningSettings
PairsSettings::forReport() const
{
    json config = {{"start", start}, {"development_end", developmentEnd}, {"validation_end", validationEnd},
                   {"end", end}, {"starting_capital", startingCapital}, {"slippage_percent", slippagePercent},
                   {"selection", {{"stocks", pairs}}}, {"gates", gates}};
    MorningSettings report = MorningSettings::fromConfig(config, end);
    for (const auto& variant : variants)
    {
        MorningSettings::Variant named;
        named.name = variant.name;
        named.step = variant.kind == Kind::Pairs ? PAIRS_STEP : LEAD_LAG_STEP;
        report.variants.push_back(named);
    }
    return report;
}
