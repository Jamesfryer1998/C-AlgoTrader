#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "../../src/util/Config.hpp"

namespace fs = std::filesystem;

namespace
{
    // A project root of its own, with config/general.json, strategy.json and risk.json in it. Config finds the
    // project root from the working folder, so each test runs from inside this one.
    class ConfigTests : public ::testing::Test
    {
        protected:
            fs::path root;
            fs::path previousFolder;

            void SetUp() override
            {
                root = fs::temp_directory_path()
                     / ("config_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
                fs::remove_all(root);
                fs::create_directories(root / "config");
                previousFolder = fs::current_path();
                fs::current_path(root);

                write(ConfigFiles::GENERAL, R"({"starting_capital": 50000, "alpaca": {"data_feed": "iex"}})");
                write(ConfigFiles::STRATEGY, R"({"portfolio": [{"ticker": "KO"}], "strategies": []})");
                write(ConfigFiles::RISK, R"({"risk": {"name": "FIXED_SIZE"}, "max_position_size": 10})");
            }

            void TearDown() override
            {
                fs::current_path(previousFolder);
                fs::remove_all(root);
            }

            void write(const std::string& configFile, const std::string& contents)
            {
                std::ofstream(root.string() + configFile) << contents;
            }
    };
}

TEST_F(ConfigTests, TheThreeFilesAreReadAsOneConfig)
{
    Config config;
    json loaded = config.loadConfig();

    EXPECT_EQ(loaded["starting_capital"], 50000);
    EXPECT_EQ(loaded["alpaca"]["data_feed"], "iex");
    EXPECT_EQ(loaded["portfolio"][0]["ticker"], "KO");
    EXPECT_EQ(loaded["risk"]["name"], "FIXED_SIZE");
    EXPECT_EQ(loaded["max_position_size"], 10);
}

TEST_F(ConfigTests, ASettingInTwoFilesIsAnError)
{
    write(ConfigFiles::RISK, R"({"risk": {"name": "FIXED_SIZE"}, "starting_capital": 1})");
    Config config;
    EXPECT_THROW(config.loadConfig(), std::runtime_error);
}

TEST_F(ConfigTests, AMissingFileIsAnError)
{
    fs::remove(root.string() + ConfigFiles::STRATEGY);
    Config config;
    EXPECT_THROW(config.loadConfig(), std::runtime_error);
}

TEST_F(ConfigTests, PathsAreUnderTheProjectRoot)
{
    Config config;
    EXPECT_EQ(config.formConfigPath(ConfigFiles::RISK), root.string() + "/config/risk.json");
}
