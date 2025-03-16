#include "Position.hpp"
        

Position::Position(
    string _ticker,
    float _quantity,
    float _avgPrice)
:
    ticker(_ticker), 
    quantity(_quantity), 
    avgPrice(_avgPrice)
{
}

Position::~Position()
{
}
