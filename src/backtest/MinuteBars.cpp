#include "MinuteBars.hpp"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>
#include "../data_access/CSVParser.hpp"
#include "../data_access/DataStitcher.hpp"
#include "../util/MarketTime.hpp"

// Load every daily CSV in the range, keeping only rows from each file's own day (so old multi-day
// Yahoo files sharing the naming scheme can't mix in), sorted oldest first.
std::vector<MarketCondition>
MinuteBars::load(const std::string& dataDir, const std::string& ticker,
                 const std::string& start, const std::string& end, int threads)
{
    DataStitcher stitcher(dataDir, ticker);
    std::vector<std::string> files = stitcher.findFilesInRange(start, end);
    std::sort(files.begin(), files.end());

    std::atomic<size_t> next{0};
    std::mutex mutex;
    std::vector<std::vector<MarketCondition>> perFile(files.size());

    std::vector<std::thread> pool;
    for (int t = 0; t < std::max(1, threads); t++)
    {
        pool.emplace_back([&]() {
            while (true)
            {
                size_t i = next.fetch_add(1);
                if (i >= files.size())
                    return;
                std::string fileDay = files[i].substr(files[i].size() - CSV_EXTENSION.size() - MarketTime::DATE_LENGTH,
                                                      MarketTime::DATE_LENGTH);   // ..._YYYY-MM-DD.csv
                try
                {
                    CSVParser parser;
                    parser.Read(files[i]);
                    for (const auto& bar : parser.GetData())
                        if (MarketTime::datePart(bar.DateTime) == fileDay)
                            perFile[i].push_back(bar);
                }
                catch (const std::exception& e)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    std::cerr << "WARNING - skipping " << files[i] << ": " << e.what() << std::endl;
                }
            }
        });
    }
    for (auto& thread : pool)
        thread.join();

    std::vector<MarketCondition> bars;
    for (auto& fileBars : perFile)
        bars.insert(bars.end(), fileBars.begin(), fileBars.end());
    std::stable_sort(bars.begin(), bars.end(), [](const MarketCondition& a, const MarketCondition& b) {
        return a.DateTime < b.DateTime;
    });
    bars.erase(std::unique(bars.begin(), bars.end(), [](const MarketCondition& a, const MarketCondition& b) {
        return a.DateTime == b.DateTime;
    }), bars.end());
    return bars;
}

