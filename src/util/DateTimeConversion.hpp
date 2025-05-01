#ifndef DATETIME_CONVERSION_HPP
#define DATETIME_CONVERSION_HPP

#include <ctime>
#include <string>

class DateTimeConversion {
public:
    DateTimeConversion();
    ~DateTimeConversion();

    tm* getTime();
    tm* getLocalTimeNow();
    std::string timeNowToDate();
    std::string timeNowToString();
    void setTime(std::time_t epochTime);

private:
    std::tm t{};
};

#endif
