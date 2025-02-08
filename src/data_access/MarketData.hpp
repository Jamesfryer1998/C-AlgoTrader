#include "CSVParser.hpp"

class MarketData
{
    public:
        MarketData();
        ~MarketData();
        void process();
        void loadData(const std::string& filePath);
        void update(std::vector<MarketCondition>& marketData);
        std::vector<MarketCondition> getData() const;

    private:
        std::vector<MarketCondition> data;
        static std::string const FILE_PATH;
};