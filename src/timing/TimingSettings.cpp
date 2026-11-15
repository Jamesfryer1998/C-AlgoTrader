#include "TimingSettings.hpp"

TimingSettings
TimingSettings::fromConfig(const json& config, const std::string& yesterday)
{
    TimingSettings settings;
    settings.start = config.value("start", std::string());
    settings.developmentEnd = config.value("development_end", std::string());
    settings.validationEnd = config.value("validation_end", std::string());
    settings.end = config.value("end", std::string());
    if (settings.end.empty())
        settings.end = yesterday;
    settings.dataStart = config.value("data_start", settings.start);

    settings.symbol = config.value("symbol", settings.symbol);
    settings.cashSymbol = config.value("cash_symbol", settings.cashSymbol);
    settings.adjustment = config.value("adjustment", settings.adjustment);
    settings.startingCapital = config.value("starting_capital", settings.startingCapital);
    settings.slippagePercent = config.value("slippage_percent", settings.slippagePercent);
    settings.rebalanceBand = config.value("rebalance_band", settings.rebalanceBand);

    json finalists = config.value("finalists", json::object());
    settings.minExposure = finalists.value("min_exposure", settings.minExposure);
    settings.maxExposure = finalists.value("max_exposure", settings.maxExposure);
    settings.minTrades = finalists.value("min_trades", settings.minTrades);
    settings.finalists = finalists.value("count", settings.finalists);
    settings.perFamily = finalists.value("per_family", settings.perFamily);
    settings.randomDraws = finalists.value("random_draws", settings.randomDraws);
    settings.leaderboardSize = finalists.value("leaderboard", settings.leaderboardSize);
    settings.randomSeed = config.value("random_seed", settings.randomSeed);

    settings.families = config.value("families", json::object());
    settings.gates = config.value("gates", json::object());
    return settings;
}

MorningSettings
TimingSettings::forReport() const
{
    json config = {{"start", start}, {"development_end", developmentEnd}, {"validation_end", validationEnd},
                   {"end", end}, {"starting_capital", startingCapital}, {"slippage_percent", slippagePercent},
                   {"selection", {{"stocks", 1}}}, {"gates", gates}};
    return MorningSettings::fromConfig(config, end);
}
