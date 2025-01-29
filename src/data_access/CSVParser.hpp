#include <iostream>
#include "MarketCondition.hpp"

class CSVParser 
{
    public:
        CSVParser();
        ~CSVParser();
        void parse(const std::string& filename);
    private:
       std::vector<MarketCondition> data;
};