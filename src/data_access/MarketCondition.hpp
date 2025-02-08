#include <iostream>

class MarketCondition
{
    public:
       MarketCondition(
        std::string _timeStamp,
        std::string _ticker,
        float _open,
        float _close,
        std::string  _timeInterval);

       ~MarketCondition();

        bool IsValid() const
        {
            return !TimeStamp.empty() && !Ticker.empty() && Open >= 0 && Close >= 0 && !TimeInterval.empty();
        }

    public:
        // Add members here
        std::string TimeStamp;
        std::string Ticker;
        float Open;
        float Close;
        std::string TimeInterval;
};
