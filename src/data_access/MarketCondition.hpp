#pragma once
#include <iostream>


class MarketCondition
{
    public:
       MarketCondition(
        std::string _timeStamp,
        std::string _ticker,
        float _open,
        float _close,
        int _volume,
        std::string _timeInterval);

       MarketCondition(){};
       ~MarketCondition();

        bool IsValid() const
        {
            return !DateTime.empty() && !Ticker.empty() && Open >= 0 && Close >= 0 && Volume >= 0 && !TimeInterval.empty();
        }

    public:
        // Add members here
        float Open;
        int Volume;
        float Close;
        std::string Ticker;
        std::string DateTime;
        std::string TimeInterval;
};
