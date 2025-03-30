#pragma once
#include "CSVParser.hpp"
#include "../util/Config.hpp"

using namespace std;

class MarketData
{
    public:
        MarketData();
        ~MarketData();
        void process(json configData);
        void loadData(const std::string& filePath);
        void update(std::vector<MarketCondition>& marketData);
        std::vector<MarketCondition> getData() const;
        string generateFilePath(json configData);
        string getProjectRoot();
        float getLastClosePrice();
        void rewind();
        bool hasNext();
        void next();

    private:
        std::vector<MarketCondition> data;
        std::vector<MarketCondition> backtestData;
        MarketCondition currentData;
        int currentIndex;
        static std::string const FILE_PATH;
};