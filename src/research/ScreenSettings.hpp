#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "ScreenOptions.hpp"

using json = nlohmann::json;

/**
 * ScreenSettings
 *
 * What one screen covers: config/screen.json's common settings with the "minute" or "daily" section on
 * top, then anything given on the command line. Works out the dates:
 *
 *   minute bars   the last history_months up to today, the final test_months being the test period
 *   daily bars    from "start", tested from "test_start"
 */
struct ScreenSettings
{
    json settings;      // what the Screener reads (with "test_start" filled in)
    std::string mode;   // "minute", "daily" or "weekly": the screen.json section, and the results folder
    bool daily = false;
    std::string start, testStart, end;
    std::vector<std::string> symbols;
    std::vector<std::string> strategies;

    // Throws if screen.json is missing or has no section for the mode
    static ScreenSettings load(const std::string& screenJsonPath, const ScreenOptions& options);
};
