#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "../../src/util/DotEnv.hpp"

TEST(DotEnvTests, ParsesSimpleLine)
{
    std::string key, value;
    ASSERT_TRUE(DotEnv::parseLine("APCA_API_KEY_ID=PK123", key, value));
    EXPECT_EQ(key, "APCA_API_KEY_ID");
    EXPECT_EQ(value, "PK123");
}

TEST(DotEnvTests, StripsQuotesSpacesAndExport)
{
    std::string key, value;
    ASSERT_TRUE(DotEnv::parseLine("  export SECRET = \"abc def\"  ", key, value));
    EXPECT_EQ(key, "SECRET");
    EXPECT_EQ(value, "abc def");

    ASSERT_TRUE(DotEnv::parseLine("SINGLE='x=y'", key, value));
    EXPECT_EQ(value, "x=y");
}

TEST(DotEnvTests, IgnoresCommentsAndBlankLines)
{
    std::string key, value;
    EXPECT_FALSE(DotEnv::parseLine("# APCA_API_KEY_ID=nope", key, value));
    EXPECT_FALSE(DotEnv::parseLine("   ", key, value));
    EXPECT_FALSE(DotEnv::parseLine("no equals sign", key, value));
}

TEST(DotEnvTests, LoadsFileButShellValueWins)
{
    std::string path = (std::filesystem::temp_directory_path() / "algo_trader_test.env").string();
    std::ofstream(path) << "# test\nDOTENV_TEST_NEW=from_file\nDOTENV_TEST_EXISTING=from_file\n";

    unsetenv("DOTENV_TEST_NEW");
    setenv("DOTENV_TEST_EXISTING", "from_shell", 1);

    DotEnv::load(path);

    EXPECT_STREQ(std::getenv("DOTENV_TEST_NEW"), "from_file");
    EXPECT_STREQ(std::getenv("DOTENV_TEST_EXISTING"), "from_shell");
    std::filesystem::remove(path);
}

TEST(DotEnvTests, MissingFileIsNotAnError)
{
    EXPECT_EQ(DotEnv::load("/definitely/not/here/.env"), 0);
}
