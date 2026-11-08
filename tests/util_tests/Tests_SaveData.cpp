#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include "../../src/util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    // A file of `bytes` bytes, last written `ageHours` ago
    void makeFile(const fs::path& path, size_t bytes, int ageHours)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path) << std::string(bytes, 'x');
        fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(ageHours));
    }

    class SaveDataTests : public ::testing::Test
    {
        protected:
            fs::path root;   // one folder per test, as ctest runs them side by side
            void SetUp() override
            {
                root = fs::temp_directory_path()
                     / ("save_data_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
                fs::remove_all(root);
            }
            void TearDown() override { fs::remove_all(root); }
    };
}

TEST_F(SaveDataTests, UnderTheLimitNothingIsDeleted)
{
    makeFile(root / "minute/AAA/a.csv", 100, 3);
    SaveData data(root.string(), 1000);
    EXPECT_EQ(data.bytesUsed(), 100u);
    EXPECT_EQ(data.enforceLimit(), 0u);
    EXPECT_TRUE(fs::exists(root / "minute/AAA/a.csv"));
}

TEST_F(SaveDataTests, OverTheLimitTheOldestGoFirst)
{
    makeFile(root / "minute/AAA/oldest.csv", 400, 30);
    makeFile(root / "minute/AAA/middle.csv", 400, 20);
    makeFile(root / "daily/newest.csv", 400, 10);
    SaveData data(root.string(), 900);

    EXPECT_EQ(data.enforceLimit(), 400u);
    EXPECT_FALSE(fs::exists(root / "minute/AAA/oldest.csv"));
    EXPECT_TRUE(fs::exists(root / "minute/AAA/middle.csv"));
    EXPECT_TRUE(fs::exists(root / "daily/newest.csv"));
}

TEST_F(SaveDataTests, LiveRecordsAndTheFileJustSavedAreNeverDeleted)
{
    makeFile(root / "live/trades.csv", 500, 100);     // oldest, but the live account's records
    makeFile(root / "minute/AAA/old.csv", 400, 50);
    makeFile(root / "minute/AAA/new.csv", 400, 1);    // just saved
    SaveData data(root.string(), 500);

    data.recordSave((root / "minute/AAA/new.csv").string());
    EXPECT_TRUE(fs::exists(root / "live/trades.csv"));
    EXPECT_TRUE(fs::exists(root / "minute/AAA/new.csv"));
    EXPECT_FALSE(fs::exists(root / "minute/AAA/old.csv"));
}

TEST_F(SaveDataTests, LiveDryRunIsNotProtected)
{
    makeFile(root / "live_dry_run/trades.csv", 400, 50);   // only data/live itself is protected
    makeFile(root / "minute/AAA/new.csv", 400, 1);
    SaveData data(root.string(), 500);
    data.enforceLimit();
    EXPECT_FALSE(fs::exists(root / "live_dry_run/trades.csv"));
}

TEST_F(SaveDataTests, TheProgramsLimitIsCheckedAtStartAndOnEverySave)
{
    makeFile(root / "minute" / "old.csv", 600, 48);
    makeFile(root / "minute" / "new.csv", 600, 1);

    // 1 KB allowed: setting it up deletes the oldest straight away
    SaveData::configure(root.string(), 1e-6);
    EXPECT_FALSE(fs::exists(root / "minute" / "old.csv"));
    EXPECT_TRUE(fs::exists(root / "minute" / "new.csv"));

    // Writing another file makes room again, keeping the one just written
    ASSERT_TRUE(SaveData::write((root / "minute" / "newest.csv").string(), std::string(600, 'y')));
    EXPECT_FALSE(fs::exists(root / "minute" / "new.csv"));
    EXPECT_TRUE(fs::exists(root / "minute" / "newest.csv"));

    EXPECT_FALSE(SaveData::write((root / "no_such_folder" / "x.csv").string(), "x"));
    SaveData::configure((root / "unused").string(), SaveData::DEFAULT_LIMIT_GB);   // leave nothing set up on root
}
