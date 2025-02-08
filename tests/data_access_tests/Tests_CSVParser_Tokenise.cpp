#include <gtest/gtest.h>
#include "../../src/data_access/CSVParser.hpp"

TEST(CSVParser, Basic_Tokenise) 
{
    CSVParser cut;

    {
        std::string input = "";
        auto tokens = cut.tokenise(input, ',');
        EXPECT_EQ(tokens.size(), 0);
    }

    {
        std::string input = "a,b,c,d,e";
        auto tokens = cut.tokenise(input, ',');
        EXPECT_EQ(tokens.size(), 5);
    }

    {
        std::string input = "a,b,c,d,e";
        auto tokens = cut.tokenise(input, ',');
        EXPECT_EQ(tokens[0], "a");
        EXPECT_EQ(tokens[1], "b");
        EXPECT_EQ(tokens[2], "c");
        EXPECT_EQ(tokens[3], "d");
        EXPECT_EQ(tokens[4], "e");
    }
}