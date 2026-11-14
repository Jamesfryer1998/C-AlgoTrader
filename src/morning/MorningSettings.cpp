#include "MorningSettings.hpp"

namespace
{
    const std::string NEXT_OPEN = "next_open";
    const std::string TOUCH = "touch";
    const std::string DAY_MOVE = "day";   // a drift variant's "direction": the day's open to close

    MorningSettings::EntryFill entryFillFrom(const json& settings, MorningSettings::EntryFill otherwise)
    {
        std::string fill = settings.value("entry_fill", std::string());
        if (fill == TOUCH)
            return MorningSettings::EntryFill::Touch;
        if (fill == NEXT_OPEN)
            return MorningSettings::EntryFill::NextOpen;
        return otherwise;
    }
}

MorningSettings
MorningSettings::fromConfig(const json& config, const std::string& yesterday)
{
    MorningSettings settings;
    settings.start = config.value("start", std::string());
    settings.developmentEnd = config.value("development_end", std::string());
    settings.validationEnd = config.value("validation_end", std::string());
    settings.end = config.value("end", std::string());
    if (settings.end.empty())
        settings.end = yesterday;

    json universe = config.value("universe", json::object());
    settings.minPrice = universe.value("min_price", settings.minPrice);
    settings.minAverageVolume = universe.value("min_average_volume", settings.minAverageVolume);
    settings.minAtr = universe.value("min_atr", settings.minAtr);
    settings.lookbackDays = universe.value("lookback_days", settings.lookbackDays);
    settings.minHistoryDays = universe.value("min_history_days", settings.minHistoryDays);

    json selection = config.value("selection", json::object());
    settings.stocks = selection.value("stocks", settings.stocks);
    settings.minRelativeVolume = selection.value("min_relative_volume", settings.minRelativeVolume);
    settings.openingMinutes = selection.value("opening_minutes", settings.openingMinutes);
    settings.randomSeed = selection.value("random_seed", settings.randomSeed);

    json orb = config.value("orb", json::object());
    settings.stopAtrFraction = orb.value("stop_atr_fraction", settings.stopAtrFraction);
    settings.riskPercent = orb.value("risk_percent", settings.riskPercent);
    settings.maxLeverage = orb.value("max_leverage", settings.maxLeverage);
    EntryFill defaultFill = entryFillFrom(orb, EntryFill::NextOpen);

    settings.driftLeverage = config.value("drift", json::object()).value("leverage", settings.driftLeverage);

    settings.startingCapital = config.value("starting_capital", settings.startingCapital);
    settings.slippagePercent = config.value("slippage_percent", settings.slippagePercent);

    for (const auto& entry : config.value("variants", json::array()))
    {
        Variant variant;
        variant.name = entry.value("name", std::string());
        variant.entryFill = entryFillFrom(entry, defaultFill);
        variant.holdMinutes = entry.value("hold_minutes", 0);
        variant.targetR = entry.value("target_r", 0.0);
        variant.strategy = entry.value("strategy", json::object());
        variant.holdDays = entry.value("hold_days", 0);
        variant.fade = entry.value("fade", false);
        variant.dayMove = entry.value("direction", std::string()) == DAY_MOVE;
        settings.variants.push_back(variant);
    }

    json gates = config.value("gates", json::object());
    settings.minMoveToCost = gates.value("min_move_to_cost", settings.minMoveToCost);
    settings.minSharpe = gates.value("min_sharpe", settings.minSharpe);
    settings.minLeadOverRandom = gates.value("min_lead_over_random", settings.minLeadOverRandom);
    settings.minTrades = gates.value("min_trades", settings.minTrades);
    settings.minDeflatedSharpe = gates.value("min_deflated_sharpe", settings.minDeflatedSharpe);
    return settings;
}

std::string
MorningSettings::periodOf(const std::string& day) const
{
    if (day <= developmentEnd)
        return DEVELOPMENT;
    if (day <= validationEnd)
        return VALIDATION;
    return HOLDOUT;
}
