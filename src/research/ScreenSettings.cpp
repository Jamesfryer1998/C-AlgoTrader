#include "ScreenSettings.hpp"

#include <ctime>
#include <fstream>
#include <stdexcept>
#include "../alpaca/AlpacaApi.hpp"
#include "../util/MarketTime.hpp"

namespace
{
    // Defaults for settings missing from screen.json
    const std::string DEFAULT_DAILY_START = AlpacaApi::FIRST_HISTORY_DAY;
    const int DEFAULT_HISTORY_MONTHS = 24;   // minute bars: two years...
    const int DEFAULT_TEST_MONTHS = 6;       // ...the last six months of which are the test period

    const std::string MINUTE = "minute";
    const std::string DAILY = "daily";

    // The common settings with one section's settings on top
    json mergeSection(const json& all, const std::string& section)
    {
        json merged = all;
        merged.erase(MINUTE);
        merged.erase(DAILY);
        for (const auto& [key, value] : all.at(section).items())
            merged[key] = value;
        return merged;
    }
}

ScreenSettings
ScreenSettings::load(const std::string& screenJsonPath, const ScreenOptions& options)
{
    std::ifstream file(screenJsonPath);
    if (!file)
        throw std::runtime_error("Missing " + screenJsonPath);
    json all = json::parse(file, nullptr, true, true);

    ScreenSettings screen;
    screen.daily = options.daily;
    screen.mode = options.daily ? DAILY : MINUTE;
    screen.settings = mergeSection(all, screen.mode);
    json& settings = screen.settings;

    if (options.maxCombinations != ScreenOptions::NOT_GIVEN)
        settings["max_combinations"] = options.maxCombinations;

    screen.symbols = options.symbols.empty() ? settings.at("universe").get<std::vector<std::string>>() : options.symbols;
    screen.strategies = options.strategies;
    if (screen.strategies.empty())
        for (const auto& [name, grid] : settings.at("strategies").items())
            screen.strategies.push_back(name);

    std::string today = MarketTime::datePart(MarketTime::toNewYorkTime(std::time(nullptr)));
    screen.end = today;
    if (screen.daily)
    {
        screen.start = settings.value("start", DEFAULT_DAILY_START);
        if (!options.testStart.empty())
            settings["test_start"] = options.testStart;
    }
    else
    {
        if (options.months != ScreenOptions::NOT_GIVEN)
            settings["history_months"] = options.months;
        if (options.testMonths != ScreenOptions::NOT_GIVEN)
            settings["test_months"] = options.testMonths;
        screen.start = MarketTime::monthsBefore(today, settings.value("history_months", DEFAULT_HISTORY_MONTHS));
        settings["test_start"] = MarketTime::monthsBefore(today, settings.value("test_months", DEFAULT_TEST_MONTHS));
    }
    screen.testStart = settings.value("test_start", "");
    return screen;
}
