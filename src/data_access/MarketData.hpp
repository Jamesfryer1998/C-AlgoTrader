#include "CSVParser.hpp"
#include "../util/Config.hpp"

using namespace std;

#ifndef PROJECT_ROOT_DIR
    #define PROJECT_ROOT_DIR ""
#endif

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