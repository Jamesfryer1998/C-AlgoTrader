#pragma once
#include "CSVParser.hpp"
#include "DataStitcher.hpp"
#include "../util/Config.hpp"
#include <chrono>
#include <future>
#include <thread>

using namespace std;

class MarketData
{
    public:
        MarketData();
        ~MarketData();
        
        // Standard process method (single file)
        void process(json configData);
        
        // New method for backtesting with date range
        void processForBacktest(json configData, const std::string& startDate, const std::string& endDate);
        
        // Parallel processing for speed
        void processParallel(json configData, int numThreads = 0);
        
        void loadData(const std::string& filePath);
        void update(std::vector<MarketCondition>& marketData);
        std::vector<MarketCondition> getData() const;
        string generateFilePath(json configData);
        string getProjectRoot();
        float getLastClosePrice();
        
        // Backtesting navigation
        void rewind();
        bool hasNext();
        void next();
        void setBacktest();
        bool reachedEnd();
        std::vector<MarketCondition> getDataUpToCurrentIndex() const;
        
        // Get the full data regardless of backtest mode (for initialization only)
        std::vector<MarketCondition> getFullData() const { return data; }
        
        // Current data access
        MarketCondition getCurrentData();
        int getCurrentIndex() const { return currentIndex; }

    private:
        std::vector<MarketCondition> data;
        MarketCondition currentData;
        int currentIndex;
        static std::string const FILE_PATH;
        bool backtest;
        
        // Helper methods
        std::string getDataDirectory();
};