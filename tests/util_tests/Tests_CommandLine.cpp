#include <gtest/gtest.h>
#include <stdexcept>
#include "../../src/util/CommandLine.hpp"

namespace
{
    // Runs `check` over a command line given as words (the app name is added)
    template <typename Check>
    void withArgs(std::vector<std::string> words, Check check)
    {
        words.insert(words.begin(), "app");
        std::vector<char*> argv;
        for (auto& word : words)
            argv.push_back(word.data());
        CommandLine args(static_cast<int>(argv.size()), argv.data());
        check(args);
    }
}

TEST(CommandLineTests, ReadsOptionsAndTheirValues)
{
    withArgs({"--daily", "--top", "3", "--value", "2500.5", "--symbols", "AAPL,,MSFT"}, [](CommandLine& args) {
        ASSERT_TRUE(args.next());
        EXPECT_TRUE(args.is("--daily"));
        EXPECT_FALSE(args.hasValue());
        ASSERT_TRUE(args.next());
        EXPECT_EQ(args.integer(), 3);
        ASSERT_TRUE(args.next());
        EXPECT_DOUBLE_EQ(args.number(), 2500.5);
        ASSERT_TRUE(args.next());
        EXPECT_EQ(args.list(), (std::vector<std::string>{"AAPL", "MSFT"}));
        EXPECT_FALSE(args.next());
    });
}

TEST(CommandLineTests, BadOrMissingValuesSayWhichOption)
{
    withArgs({"--top", "3x"}, [](CommandLine& args) {
        args.next();
        try { args.integer(); FAIL(); }
        catch (const std::invalid_argument& e) { EXPECT_STREQ(e.what(), "Bad value for --top: 3x"); }
    });
    withArgs({"--top", "--daily"}, [](CommandLine& args) {
        args.next();
        try { args.integer(); FAIL(); }
        catch (const std::invalid_argument& e) { EXPECT_STREQ(e.what(), "Missing value for --top"); }
    });
    withArgs({"--tpo"}, [](CommandLine& args) {
        args.next();
        EXPECT_THROW(args.rejectOption(), std::invalid_argument);
    });
}

TEST(CommandLineTests, ListsOfNumbers)
{
    withArgs({"--weights", "0.5,0.3,,0.2"}, [](CommandLine& args) {
        args.next();
        EXPECT_EQ(args.numbers(), (std::vector<double>{0.5, 0.3, 0.2}));
    });
    withArgs({"--weights", "0.5,half"}, [](CommandLine& args) {
        args.next();
        EXPECT_THROW(args.numbers(), std::invalid_argument);
    });
}

TEST(CommandLineTests, NumbersMustBeWholeNumbers)
{
    // "2.5x" and "abc" aren't numbers, and 99999999999 is too big for an int
    for (const char* value : {"2.5x", "abc"})
        withArgs({"--speed", value}, [](CommandLine& args) {
            args.next();
            EXPECT_THROW(args.number(), std::invalid_argument);
        });
    withArgs({"--top", "99999999999"}, [](CommandLine& args) {
        args.next();
        EXPECT_THROW(args.integer(), std::invalid_argument);
    });
}
