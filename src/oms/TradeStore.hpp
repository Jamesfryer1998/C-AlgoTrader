#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "Order.hpp"
#include "Position.hpp"

using json = nlohmann::json;

/**
 * TradeRecord
 *
 * One line of data/live/trades.csv: an order the system sent to the broker.
 */
struct TradeRecord
{
    std::string time;        // bar time it was decided on (New York)
    std::string ticker;
    std::string type;        // BUY, SELL, LIMIT_BUY, ...
    double quantity = 0;
    double price = 0;        // bar close when sent (an estimate of the fill price)
    double stopLoss = 0;
    double takeProfit = 0;

    bool isBuy() const { return type == "BUY" || type == "LIMIT_BUY" || type == "STOP_BUY"; }
};

/**
 * TradeStore
 *
 * Saves live trading state to disk so a restart can carry on where it left off.
 * Files are only written while running and only read once, at start-up, so they never slow down
 * a trading decision.
 *
 *   <dir>/trades.csv        every order sent, appended one line at a time
 *   <dir>/positions.csv     current positions, rewritten when they change
 *   <dir>/performance.json  latest P&L snapshot, rewritten every bar
 *
 * Rewritten files are written to a temporary file first and then renamed, so a crash mid-write
 * never leaves a half-written file.
 */
class TradeStore
{
    public:
        TradeStore(const std::string& directory);

        void appendTrade(const TradeRecord& trade);
        void savePositions(const std::vector<Position>& positions);
        void savePerformance(const json& performance);

        std::vector<TradeRecord> loadTrades() const;
        std::vector<Position> loadPositions() const;

        const std::string& getDirectory() const { return directory; }

    private:
        void writeAtomically(const std::string& fileName, const std::string& contents);
        std::string path(const std::string& fileName) const;

        std::string directory;
};
