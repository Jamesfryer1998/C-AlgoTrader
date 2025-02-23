#include "Position.hpp"
        

Position::Position(
    int _id,
    string _ticker,
    float _quantity,
    float _avgPrice)
:
    id(_id),
    ticker(_ticker), 
    quantity(_quantity), 
    avgPrice(_avgPrice)
{
}

Position::~Position()
{
}
