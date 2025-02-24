#pragma once
#include "CSVParser.hpp"
#include "../util/Config.hpp"

using namespace std;

class MarketData
{
    public:
        MarketData();
        ~MarketData();
        void process();
        void loadData(const std::string& filePath);
        void update(std::vector<MarketCondition>& marketData);
        std::vector<MarketCondition> getData() const;
        string generateFilePath(json configData);
        string getProjectRoot();

    private:
        std::vector<MarketCondition> data;
        static std::string const FILE_PATH;
};