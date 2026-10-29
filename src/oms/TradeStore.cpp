#include "TradeStore.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

static const char* TRADES_FILE = "trades.csv";
static const char* POSITIONS_FILE = "positions.csv";
static const char* PERFORMANCE_FILE = "performance.json";

static std::vector<std::string> splitCsv(const std::string& line)
{
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ','))
        fields.push_back(field);
    return fields;
}

TradeStore::TradeStore(const std::string& dir)
: directory(dir)
{
    std::filesystem::create_directories(directory);
}

std::string
TradeStore::path(const std::string& fileName) const
{
    return (std::filesystem::path(directory) / fileName).string();
}

void
TradeStore::writeAtomically(const std::string& fileName, const std::string& contents)
{
    std::string finalPath = path(fileName);
    std::string tempPath = finalPath + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::trunc);
        if (!file)
        {
            std::cerr << "WARNING - Could not write " << tempPath << std::endl;
            return;
        }
        file << contents;
    }
    std::filesystem::rename(tempPath, finalPath);
}

void
TradeStore::appendTrade(const TradeRecord& trade)
{
    std::string filePath = path(TRADES_FILE);
    bool isNew = !std::filesystem::exists(filePath);

    std::ofstream file(filePath, std::ios::app);
    if (!file)
    {
        std::cerr << "WARNING - Could not write " << filePath << std::endl;
        return;
    }
    if (isNew)
        file << "time,ticker,type,quantity,price,stop_loss,take_profit\n";

    file << std::fixed << std::setprecision(4)
         << trade.time << "," << trade.ticker << "," << trade.type << "," << trade.quantity << ","
         << trade.price << "," << trade.stopLoss << "," << trade.takeProfit << "\n";
    file.flush();
}

void
TradeStore::savePositions(const std::vector<Position>& positions)
{
    std::ostringstream out;
    out << "ticker,quantity,avg_price\n" << std::fixed << std::setprecision(4);
    for (const auto& position : positions)
        out << position.getTicker() << "," << position.getQuantity() << "," << position.getAvgPrice() << "\n";
    writeAtomically(POSITIONS_FILE, out.str());
}

void
TradeStore::savePerformance(const json& performance)
{
    writeAtomically(PERFORMANCE_FILE, performance.dump(2) + "\n");
}

std::vector<TradeRecord>
TradeStore::loadTrades() const
{
    std::vector<TradeRecord> trades;
    std::ifstream file(path(TRADES_FILE));
    std::string line;
    std::getline(file, line); // header

    while (std::getline(file, line))
    {
        std::vector<std::string> f = splitCsv(line);
        if (f.size() != 7)
            continue;
        try
        {
            TradeRecord t;
            t.time = f[0];
            t.ticker = f[1];
            t.type = f[2];
            t.quantity = std::stod(f[3]);
            t.price = std::stod(f[4]);
            t.stopLoss = std::stod(f[5]);
            t.takeProfit = std::stod(f[6]);
            trades.push_back(t);
        }
        catch (const std::exception&)
        {
            std::cerr << "WARNING - Skipping bad line in trades.csv: " << line << std::endl;
        }
    }
    return trades;
}

std::vector<Position>
TradeStore::loadPositions() const
{
    std::vector<Position> positions;
    std::ifstream file(path(POSITIONS_FILE));
    std::string line;
    std::getline(file, line); // header

    while (std::getline(file, line))
    {
        std::vector<std::string> f = splitCsv(line);
        if (f.size() != 3)
            continue;
        try
        {
            positions.emplace_back(f[0], std::stof(f[1]), std::stof(f[2]));
        }
        catch (const std::exception&)
        {
            std::cerr << "WARNING - Skipping bad line in positions.csv: " << line << std::endl;
        }
    }
    return positions;
}
