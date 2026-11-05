#pragma once

#include <string>
#include <vector>

/**
 * CommandLine
 *
 * Walks through an app's options one at a time, with readable errors for bad values:
 *
 *   CommandLine args(argc, argv);
 *   while (args.next())
 *   {
 *       if (args.is("--daily"))        daily = true;
 *       else if (args.is("--top"))     top = args.whole();
 *       else if (args.is("--symbols")) symbols = args.list();
 *       else                           args.rejectOption();
 *   }
 *
 * Every problem throws std::invalid_argument, e.g. "Bad value for --top: x" or "Unknown option: --tpo".
 */
class CommandLine
{
    public:
        CommandLine(int argc, char* argv[], int firstOption = 1);

        // Move to the next option. False when there are none left.
        bool next();

        bool is(const std::string& name) const { return current == name; }
        const std::string& option() const { return current; }

        // Is the next word a value (not another option)?
        bool hasValue() const;

        // The value after the current option
        std::string text();
        double number();
        int whole();
        std::vector<std::string> list();   // "AAPL,MSFT,NVDA" -> {"AAPL", "MSFT", "NVDA"}
        std::vector<double> numbers();     // "0.5,0.3,0.2"

        [[noreturn]] void rejectOption() const;

        // "AAPL,MSFT" -> {"AAPL", "MSFT"} (empty parts dropped)
        static std::vector<std::string> split(const std::string& text);

    private:
        [[noreturn]] void badValue(const std::string& value) const;
        double toNumber(const std::string& value) const;

        std::vector<std::string> words;
        size_t position;
        std::string current;
};
