#include "DateTimeConversion.hpp"
#include <iomanip>
#include <sstream>

namespace
{
    const char* const DATE_TIME_FORMAT = "%Y-%m-%d %H:%M:%S";   // 2025-03-31 14:30:00
    const char* const DATE_FORMAT = "%Y-%m-%d";                 // 2025-03-31
}

DateTimeConversion::DateTimeConversion()
{
    getLocalTimeNow();
}

DateTimeConversion::~DateTimeConversion()
{
}

tm* 
DateTimeConversion::getLocalTimeNow() 
{
    std::time_t now = std::time(nullptr);
    t = *std::localtime(&now);
    return &t;
}

tm* 
DateTimeConversion::getTime()
{
    return &t;
}

void 
DateTimeConversion::setTime(std::time_t epochTime) 
{  
    t = *std::gmtime(&epochTime);
}

std::string DateTimeConversion::timeNowToString()
{
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), DATE_TIME_FORMAT, &t);
    return std::string(buffer);
}

std::string DateTimeConversion::timeNowToDate()
{
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), DATE_FORMAT, &t);
    return std::string(buffer);
}
