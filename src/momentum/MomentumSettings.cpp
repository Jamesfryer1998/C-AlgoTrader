#include "MomentumSettings.hpp"

namespace
{
    const std::string LAST_HALF_HOUR = "last_half_hour", OVERNIGHT = "overnight";
    const std::string SEPARATOR = ": ";
}

MomentumSettings
MomentumSettings::fromConfig(const json& config, const std::string& yesterday)
{
    MomentumSettings settings;
    settings.start = config.value("start", std::string());
    settings.developmentEnd = config.value("development_end", std::string());
    settings.validationEnd = config.value("validation_end", std::string());
    settings.end = config.value("end", std::string());
    if (settings.end.empty())
        settings.end = yesterday;
    settings.dataStart = config.value("data_start", settings.start);
    settings.symbols = config.value("symbols", std::vector<std::string>());
    settings.benchmark = config.value("benchmark", settings.benchmark);
    settings.startingCapital = config.value("starting_capital", settings.startingCapital);
    settings.randomSeed = config.value("random_seed", settings.randomSeed);

    json defaults = config.value("noise_area", json::object());
    for (const auto& entry : config.value("variants", json::array()))
    {
        json merged = defaults;
        merged.update(entry);
        Variant variant;
        variant.name = merged.value("name", std::string());
        std::string kind = merged.value("kind", std::string());
        variant.kind = kind == LAST_HALF_HOUR ? Kind::LastHalfHour : kind == OVERNIGHT ? Kind::Overnight : Kind::NoiseArea;
        variant.bandMultiplier = merged.value("band_multiplier", variant.bandMultiplier);
        variant.lookbackDays = merged.value("lookback_days", variant.lookbackDays);
        variant.checkMinutes = merged.value("check_minutes", variant.checkMinutes);
        variant.vwapStop = merged.value("vwap_stop", variant.vwapStop);
        variant.targetVolatility = merged.value("target_daily_volatility", variant.targetVolatility);
        variant.maxLeverage = merged.value("max_leverage", variant.maxLeverage);
        variant.costPerShare = merged.value("cost_per_share", variant.costPerShare);
        variant.slippagePercent = merged.value("slippage_percent", variant.slippagePercent);
        settings.variants.push_back(variant);
    }
    settings.gates = config.value("gates", json::object());
    return settings;
}

std::string
MomentumSettings::lineName(const std::string& symbol, const Variant& variant)
{
    return symbol + SEPARATOR + variant.name;
}

MorningSettings
MomentumSettings::forReport() const
{
    json config = {{"start", start}, {"development_end", developmentEnd}, {"validation_end", validationEnd},
                   {"end", end}, {"starting_capital", startingCapital}, {"slippage_percent", 0.0},
                   {"selection", {{"stocks", symbols.size()}}}, {"gates", gates}};
    MorningSettings report = MorningSettings::fromConfig(config, end);
    for (const auto& symbol : symbols)
        for (const auto& variant : variants)
        {
            MorningSettings::Variant named;
            named.name = lineName(symbol, variant);
            named.step = variant.kind == Kind::NoiseArea ? NOISE_STEP
                       : variant.kind == Kind::LastHalfHour ? HALF_HOUR_STEP
                                                            : OVERNIGHT_STEP;
            report.variants.push_back(named);
        }
    return report;
}
