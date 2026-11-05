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
        EXPECT_EQ(args.whole(), 3);
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
        try { args.whole(); FAIL(); }
        catch (const std::invalid_argument& e) { EXPECT_STREQ(e.what(), "Bad value for --top: 3x"); }
    });
    withArgs({"--top", "--daily"}, [](CommandLine& args) {
        args.next();
        try { args.whole(); FAIL(); }
        catch (const std::invalid_argument& e) { EXPECT_STREQ(e.what(), "Missing value for --top"); }
    });
    withArgs({"--tpo"}, [](CommandLine& args) {
        args.next();
        EXPECT_THROW(args.rejectOption(), std::invalid_argument);
    });
}
