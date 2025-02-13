#ifndef DATETIME_CONVERSION_HPP
#define DATETIME_CONVERSION_HPP

#include <ctime>
#include <string>

class DateTimeConversion {
public:
    DateTimeConversion();
    ~DateTimeConversion();

    tm* getLocalTimeNow();
    tm* getTime();
    void setTime(std::time_t epochTime);
    std::string timeNowToString();
    std::string timeNowToDate();

private:
    std::tm t{};
};

#endif
