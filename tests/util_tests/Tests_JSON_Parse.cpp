#include <gtest/gtest.h>
#include "../../src/util/Config.hpp"

TEST(JSONParser, CanBeInstantiated)
{
    Config cut;
}

TEST(JSONParser, CanLoadFile)
{
    Config cut;
    cut.loadJson("/Users/james/Projects/C++AlgoTrader/tests/util_tests/test_data/config_test.json");
}

TEST(JSONParser, ReturnsCorrectNumberOfValues)
{
    Config cut;
    cut.loadJson("/Users/james/Projects/C++AlgoTrader/tests/util_tests/test_data/config_test.json");
    EXPECT_EQ(cut.getJson().size(), 5);
}

TEST(JSONParser, ReturnsCorrectInteger)
{
    Config cut;
    cut.loadJson("/Users/james/Projects/C++AlgoTrader/tests/util_tests/test_data/config_test.json");
    EXPECT_EQ(cut.getJson()["a"], 1);
}

TEST(JSONParser, ReturnsCorrectFloat)
{
    Config cut;
    cut.loadJson("/Users/james/Projects/C++AlgoTrader/tests/util_tests/test_data/config_test.json");
    EXPECT_EQ(cut.getJson()["b"], 2.0);
}

TEST(JSONParser, ReturnsCorrectLongInt)
{
    Config cut;
    cut.loadJson("/Users/james/Projects/C++AlgoTrader/tests/util_tests/test_data/config_test.json");
    EXPECT_EQ(cut.getJson()["c"], 2147483647);
}

TEST(JSONParser, ReturnsCorrectDouble)
{
    Config cut;
    cut.loadJson("/Users/james/Projects/C++AlgoTrader/tests/util_tests/test_data/config_test.json");
    EXPECT_EQ(cut.getJson()["d"], 0.00000000000000004);
}

TEST(JSONParser, ReturnsCorrectString)
{
    Config cut;
    cut.loadJson("/Users/james/Projects/C++AlgoTrader/tests/util_tests/test_data/config_test.json");
    EXPECT_EQ(cut.getJson()["e"], "5");
}