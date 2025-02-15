#include "DateTimeConversion.hpp"
#include <iomanip>
#include <sstream>

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
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &t);
    return std::string(buffer);
}

std::string DateTimeConversion::timeNowToDate()
{
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &t);
    return std::string(buffer);
}
