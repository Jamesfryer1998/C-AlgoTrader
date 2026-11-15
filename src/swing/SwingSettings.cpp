#include "SwingSettings.hpp"

namespace
{
    const std::string BREAKOUT = "breakout";
    const std::string LOSERS = "losers";
    const std::string AT_CLOSE = "close";
}

SwingSettings
SwingSettings::fromConfig(const json& config, const std::string& yesterday)
{
    SwingSettings settings;
    settings.start = config.value("start", std::string());
    settings.developmentEnd = config.value("development_end", std::string());
    settings.validationEnd = config.value("validation_end", std::string());
    settings.end = config.value("end", std::string());
    if (settings.end.empty())
        settings.end = yesterday;
    settings.dataStart = config.value("data_start", settings.start);

    json universe = config.value("universe", json::object());
    settings.minPrice = universe.value("min_price", settings.minPrice);
    settings.minDollarVolume = universe.value("min_dollar_volume", settings.minDollarVolume);
    settings.dollarVolumeDays = universe.value("dollar_volume_days", settings.dollarVolumeDays);
    settings.prefilterDollarVolume = universe.value("prefilter_dollar_volume", settings.prefilterDollarVolume);
    settings.loadDollarVolume = universe.value("load_dollar_volume", settings.loadDollarVolume);

    json pullback = config.value("pullback", json::object());
    settings.trendDays = pullback.value("trend_days", settings.trendDays);
    settings.rsiDays = pullback.value("rsi_days", settings.rsiDays);
    settings.rsiBelow = pullback.value("rsi_below", settings.rsiBelow);
    settings.exitAboveDays = pullback.value("exit_above_days", settings.exitAboveDays);
    settings.pullbackMaxHold = pullback.value("max_hold_days", settings.pullbackMaxHold);

    json breakout = config.value("breakout", json::object());
    settings.highDays = breakout.value("high_days", settings.highDays);
    settings.volumeDays = breakout.value("volume_days", settings.volumeDays);
    settings.volumeRatio = breakout.value("volume_ratio", settings.volumeRatio);
    settings.atrDays = breakout.value("atr_days", settings.atrDays);
    settings.stopAtr = breakout.value("stop_atr", settings.stopAtr);
    settings.breakoutHold = breakout.value("hold_days", settings.breakoutHold);

    json losers = config.value("losers", json::object());
    settings.returnDays = losers.value("return_days", settings.returnDays);
    settings.losersHold = losers.value("hold_days", settings.losersHold);

    settings.positions = config.value("positions", settings.positions);
    settings.startingCapital = config.value("starting_capital", settings.startingCapital);
    settings.slippagePercent = config.value("slippage_percent", settings.slippagePercent);
    settings.leverage = config.value("leverage", settings.leverage);
    settings.randomSeed = config.value("random_seed", settings.randomSeed);

    for (const auto& entry : config.value("variants", json::array()))
    {
        Variant variant;
        variant.name = entry.value("name", std::string());
        std::string kind = entry.value("kind", std::string());
        variant.kind = kind == BREAKOUT ? Kind::Breakout : kind == LOSERS ? Kind::Losers : Kind::Pullback;
        variant.entry = entry.value("entry", std::string()) == AT_CLOSE ? Entry::SignalClose : Entry::NextOpen;
        variant.symbols = entry.value("symbols", std::vector<std::string>());
        variant.positions = entry.value("positions", variant.positions);
        settings.variants.push_back(variant);
    }
    settings.gates = config.value("gates", json::object());
    return settings;
}

MorningSettings
SwingSettings::forReport() const
{
    json config = {{"start", start}, {"development_end", developmentEnd}, {"validation_end", validationEnd},
                   {"end", end}, {"starting_capital", startingCapital}, {"slippage_percent", slippagePercent},
                   {"selection", {{"stocks", positions}}}, {"gates", gates}};
    MorningSettings report = MorningSettings::fromConfig(config, end);
    for (const auto& variant : variants)
    {
        MorningSettings::Variant named;
        named.name = variant.name;
        named.step = variant.kind == Kind::Breakout ? BREAKOUT_STEP
                   : variant.kind == Kind::Losers   ? LOSERS_STEP
                                                     : PULLBACK_STEP;
        report.variants.push_back(named);
    }
    return report;
}
