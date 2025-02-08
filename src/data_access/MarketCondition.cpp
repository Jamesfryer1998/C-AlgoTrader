#include "MarketCondition.hpp"

MarketCondition::MarketCondition(
    std::string _timeStamp,
    std::string _ticker,
    float _open,
    float _close,
    std::string _timeInterval)

: TimeStamp(_timeStamp),
Ticker(_ticker),
Open(_open),
Close(_close),
TimeInterval(_timeInterval)
{

}

MarketCondition::~MarketCondition() 
{

}