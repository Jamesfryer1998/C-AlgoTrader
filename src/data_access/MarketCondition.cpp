#include "MarketCondition.hpp"

MarketCondition::MarketCondition(
    std::string _dateTime,
    std::string _ticker,
    float _open,
    float _close,
    int _volume,
    std::string _timeInterval)

: DateTime(_dateTime),
Ticker(_ticker),
Open(_open),
Close(_close),
Volume(_volume),
TimeInterval(_timeInterval)
{

}

MarketCondition::~MarketCondition() 
{

}