#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../../src/research/RiskTuning.hpp"

TEST(RiskTuningTests, GridHasEveryCombination)
{
    json config = {{"risk_tuning", {{"grids", {{"RISK_PER_TRADE", {{"risk_percent", {0.5, 1}}, {"max_stock_percent", {20, 40}}}}}}}}};
    std::vector<json> grid = RiskTuning::grid(config, {"RISK_PER_TRADE"});
    ASSERT_EQ(grid.size(), 4u);
    for (const auto& block : grid)
        EXPECT_EQ(block["name"], "RISK_PER_TRADE");

    // Engines not listed in the config fall back to the defaults
    EXPECT_FALSE(RiskTuning::grid(json::object(), {"FIXED_FRACTION"}).empty());
}

TEST(RiskTuningTests, OneLinePutsTheNameFirst)
{
    json risk = {{"risk_percent", 0.5}, {"name", "RISK_PER_TRADE"}};
    EXPECT_EQ(RiskTuning::oneLine(risk), R"({"name": "RISK_PER_TRADE", "risk_percent": 0.5})");
}

TEST(RiskTuningTests, SaveReplacesOnlyTheRiskLine)
{
    std::string path = (std::filesystem::temp_directory_path() / "risk_tuning_test_config.json").string();
    std::string original = "{\n    \"ticker\": \"NVDA\",\n    \"risk\": {\"name\": \"FIXED_SIZE\", \"quantity\": 1},\n"
                           "    \"risk_comparison\": [\n        {\"name\": \"FIXED_SIZE\", \"quantity\": 1}\n    ]\n}\n";
    std::ofstream(path) << original;

    json chosen = {{"name", "FIXED_FRACTION"}, {"position_percent", 10}};
    ASSERT_TRUE(RiskTuning::saveToConfig(path, chosen, "first"));
    ASSERT_TRUE(RiskTuning::saveToConfig(path, chosen, "second"));   // the note is replaced, not repeated

    std::stringstream saved;
    saved << std::ifstream(path).rdbuf();
    EXPECT_EQ(saved.str(), "{\n    \"ticker\": \"NVDA\",\n    \"_risk_tuned\": \"second\",\n"
                           "    \"risk\": {\"name\": \"FIXED_FRACTION\", \"position_percent\": 10},\n"
                           "    \"risk_comparison\": [\n        {\"name\": \"FIXED_SIZE\", \"quantity\": 1}\n    ]\n}\n");
    std::filesystem::remove(path);
}
