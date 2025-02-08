#include <gtest/gtest.h>
#include "../../src/util/Config.hpp"

TEST(JSONParser, CanBeInstantiated)
{
    Config config;
}

TEST(JSONParser, CanLoadFile)
{
    Config config;
    config.loadJson("tests/util_tests/test_data/config_test.json");
}