#include <string>
#include <iostream>

namespace DecimalFunctions {

    // Matches: DecimalFunctions::decimalToString(unsigned long long)
    std::__1::string decimalToString(unsigned long long value)  {
        return std::to_string(value);
    }

    // Matches: DecimalFunctions::stringToDecimal(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>)
    unsigned long long stringToDecimal(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> str) {
        try {
            double value = std::stod(str);  // Convert to double
            // IB Decimal format uses 4 digits of precision (e.g., 1.2345 => 12345)
            unsigned long long decimal = static_cast<unsigned long long>(std::round(value * 10000));
            return decimal;
        } catch (const std::exception& e) {
            std::cerr << "[Decimal Error] stringToDecimal failed: '" << str << "' — " << e.what() << std::endl;
            return 0;
        }
    }
    
    std::string decimalStringToDisplay(unsigned long long value) {
        return std::to_string(value);  // or return ""; if you don't care
    }

};