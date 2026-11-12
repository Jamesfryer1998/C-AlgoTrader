#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "../../src/research/ScreenOptions.hpp"
#include "../../src/research/ScreenSettings.hpp"

namespace fs = std::filesystem;

namespace
{
    // A screen.json with a minute section, a weekly section on top of it, and a daily one
    const char* SCREEN_JSON = R"({
        "universe": ["KO", "PEP"],
        "max_combinations": 0,
        "minute": {"history_months": 24, "test_months": 6, "min_closed_trades": 30,
                   "strategies": {"RSI": {"period": [7, 14]}}},
        "weekly": {"history_months": 6, "test_months": 1, "min_closed_trades": 10, "max_combinations": 150},
        "daily": {"start": "2016-01-01", "test_start": "2024-01-01", "strategies": {"MACD": {}}}
    })";

    ScreenSettings load(std::vector<const char*> arguments)
    {
        fs::path file = fs::temp_directory_path() / "screen_settings_test.json";
        std::ofstream(file) << SCREEN_JSON;
        arguments.insert(arguments.begin(), "screen_app");
        ScreenOptions options = ScreenOptions::parse(static_cast<int>(arguments.size()), const_cast<char**>(arguments.data()));
        return ScreenSettings::load(file.string(), options);
    }
}

TEST(ScreenSettingsTests, WeeklyIsTheMinuteScreenWithItsOwnSettingsOnTop)
{
    ScreenSettings weekly = load({"--weekly"});
    EXPECT_EQ(weekly.mode, "weekly");                                 // its own results folder
    EXPECT_FALSE(weekly.daily);                                       // 1-minute bars
    EXPECT_EQ(weekly.settings["history_months"], 6);
    EXPECT_EQ(weekly.settings["min_closed_trades"], 10);
    EXPECT_EQ(weekly.settings["max_combinations"], 150);
    EXPECT_TRUE(weekly.settings["strategies"].contains("RSI"));      // the minute section's grids
    EXPECT_FALSE(weekly.settings.contains("weekly"));
    EXPECT_LT(weekly.start, weekly.testStart);                        // tuned on the months before the test month
    EXPECT_LT(weekly.testStart, weekly.end);

    ScreenSettings minute = load({});
    EXPECT_EQ(minute.mode, "minute");
    EXPECT_EQ(minute.settings["min_closed_trades"], 30);               // the weekly settings don't leak in
    EXPECT_LT(minute.start, weekly.start);                            // 24 months back, not 6
}

TEST(ScreenSettingsTests, WeeklyCantBeDaily)
{
    EXPECT_THROW(load({"--weekly", "--daily"}), std::invalid_argument);
}
