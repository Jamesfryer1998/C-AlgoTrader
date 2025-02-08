#ifndef CSVPARSER_HPP
#define CSVPARSER_HPP

#include <iostream>
#include "MarketCondition.hpp"

class CSVParser 
{
    public:
        CSVParser();
        ~CSVParser();
        void Read(const std::string& filename);
        MarketCondition ParseToMarketCondition(std::string line);
        std::vector<std::string> tokenise(std::string csvLine, char separator);
        std::vector<MarketCondition>& GetData(){ return data; };

    private:
       std::vector<MarketCondition> data;
};

#endif