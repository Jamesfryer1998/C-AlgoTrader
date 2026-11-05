#pragma once

#include <iostream>
#include <sstream>
#include <string>

/**
 * OutputCapture
 *
 * While alive, everything written to std::cout and std::cerr goes into a buffer instead of the terminal.
 * The live app uses it to collect what a bar's decision printed (orders sent, rejections) and list it
 * under that minute's table.
 */
class OutputCapture
{
    public:
        OutputCapture()
            : originalOut(std::cout.rdbuf(buffer.rdbuf())),
              originalErr(std::cerr.rdbuf(buffer.rdbuf()))
        {
        }

        ~OutputCapture() { restore(); }

        // Stop capturing and return what was written
        std::string release()
        {
            restore();
            return buffer.str();
        }

    private:
        void restore()
        {
            std::cout.rdbuf(originalOut);
            std::cerr.rdbuf(originalErr);
        }

        std::ostringstream buffer;
        std::streambuf* originalOut;
        std::streambuf* originalErr;
};
