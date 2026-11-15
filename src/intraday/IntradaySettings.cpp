#include "IntradaySettings.hpp"

namespace
{
    const std::string RIDE = "ride";
}

IntradaySettings
IntradaySettings::fromConfig(const json& config, const std::string& yesterday)
{
    IntradaySettings settings;
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
    settings.maxCorrelation = universe.value("max_correlation", settings.maxCorrelation);
    settings.prefilterDollarVolume = universe.value("prefilter_dollar_volume", settings.prefilterDollarVolume);
    settings.loadDollarVolume = universe.value("load_dollar_volume", settings.loadDollarVolume);
    settings.funds = config.value("funds", std::vector<std::string>());
    settings.notStocks = config.value("not_stocks", std::vector<std::string>());

    json day = config.value("day", json::object());
    settings.firstMinute = day.value("first_minute", settings.firstMinute);
    settings.lastEntryMinute = day.value("last_entry_minute", settings.lastEntryMinute);
    settings.stepMinutes = day.value("step_minutes", settings.stepMinutes);

    settings.stocksEachSide = config.value("stocks_each_side", settings.stocksEachSide);
    settings.entryZ = config.value("entry_z", settings.entryZ);
    settings.exitZ = config.value("exit_z", settings.exitZ);

    settings.startingCapital = config.value("starting_capital", settings.startingCapital);
    settings.slippagePercent = config.value("slippage_percent", settings.slippagePercent);
    settings.leverage = config.value("leverage", settings.leverage);
    settings.randomSeed = config.value("random_seed", settings.randomSeed);

    for (const auto& entry : config.value("variants", json::array()))
    {
        Variant variant;
        variant.name = entry.value("name", std::string());
        variant.direction = entry.value("direction", std::string()) == RIDE ? Direction::Ride : Direction::Fade;
        variant.lookbackMinutes = entry.value("lookback_minutes", variant.lookbackMinutes);
        variant.holdMinutes = entry.value("hold_minutes", variant.holdMinutes);
        variant.delayMinutes = entry.value("delay_minutes", variant.delayMinutes);
        variant.hedge = entry.value("hedge", variant.hedge);
        settings.variants.push_back(variant);
    }
    settings.gates = config.value("gates", json::object());
    return settings;
}

MorningSettings
IntradaySettings::forReport() const
{
    json config = {{"start", start}, {"development_end", developmentEnd}, {"validation_end", validationEnd},
                   {"end", end}, {"starting_capital", startingCapital}, {"slippage_percent", slippagePercent},
                   {"selection", {{"stocks", stocks}}}, {"gates", gates}};
    MorningSettings report = MorningSettings::fromConfig(config, end);
    for (const auto& variant : variants)
    {
        MorningSettings::Variant named;
        named.name = variant.name;
        named.step = variant.direction == Direction::Fade ? FADE_STEP : RIDE_STEP;
        report.variants.push_back(named);
    }
    return report;
}
