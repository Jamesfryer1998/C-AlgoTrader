#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>
#include "Order.hpp"
#include "Position.hpp"

using json = nlohmann::json;

/**
 * TradeRecord
 *
 * One line of data/live/trades.csv: every order the strategies produced, whatever happened to it.
 *
 *   status:  sent        accepted by the broker (broker_status has the broker's own word, e.g. "accepted")
 *            rejected    the broker refused it (note has the reason)
 *            blocked     our risk checks stopped it before it was sent (note has the reason)
 *            adjustment  not an order we sent: the broker's position differed from ours, e.g. Alpaca
 *                        filled a stop loss / take profit by itself, so we record the difference
 *            dry_run     dry run, printed instead of sent
 */
// A trade's status in trades.csv (see above)
namespace TradeStatus
{
    inline const std::string SENT = "sent";
    inline const std::string REJECTED = "rejected";
    inline const std::string BLOCKED = "blocked";
    inline const std::string ADJUSTMENT = "adjustment";
    inline const std::string DRY_RUN = "dry_run";
}

// A trade's side
inline const std::string BUY_SIDE = "buy";
inline const std::string SELL_SIDE = "sell";

struct TradeRecord
{
    int ourId = 0;                // our order number, also inside client_order_id
    std::string time;             // bar the decision was made on (New York time)
    std::string sentAtUtc;        // wall-clock time the order was sent, e.g. 2026-10-29T18:31:00.123Z
    std::string strategy;         // e.g. RSI
    std::string ticker;
    std::string side;             // buy / sell
    std::string type;             // BUY, SELL, LIMIT_BUY, ...
    std::string orderClass;       // simple / bracket
    double quantity = 0;
    double price = 0;             // bar close when decided (estimate of the fill price)
    double stopLoss = 0;
    double takeProfit = 0;
    std::string status;           // see above
    std::string brokerStatus;     // the broker's own status for the order
    std::string clientOrderId;    // id we sent to the broker
    std::string brokerOrderId;    // id the broker gave the order
    double roundTripMs = 0;       // time to send the order and get the broker's reply
    std::string note;             // reason for blocked / rejected / adjustment

    bool isBuy() const { return side == BUY_SIDE; }

    // Did this line change our position? (sent orders, dry runs and adjustments do; blocked/rejected don't)
    bool changedPosition() const { return status == TradeStatus::SENT || status == TradeStatus::ADJUSTMENT
                                         || status == TradeStatus::DRY_RUN; }
};

/**
 * TradeStore
 *
 * Saves live trading state to disk so a restart can carry on where it left off.
 *
 *   <dir>/trades.csv        every order attempt, appended one line at a time
 *   <dir>/positions.csv     current positions, rewritten when they change
 *   <dir>/performance.json  latest P&L snapshot, rewritten every bar
 *
 * Writing never slows trading: save calls just queue the work and return straight away; a background
 * thread does the disk writes in order. Files are only read once, at start-up. Rewritten files go to a
 * temporary file first and are then renamed, so a crash mid-write never leaves a half-written file.
 */
class TradeStore
{
    public:
        static const std::vector<std::string> TRADE_COLUMNS;

        TradeStore(const std::string& directory);
        ~TradeStore();   // finishes any queued writes

        void appendTrade(const TradeRecord& trade);
        void savePositions(const std::vector<Position>& positions);
        void savePerformance(const json& performance);

        // Wait until every queued write is on disk (used by tests and at shutdown)
        void flush();

        std::vector<TradeRecord> loadTrades() const;
        std::vector<Position> loadPositions() const;

        const std::string& getDirectory() const { return directory; }

    private:
        void enqueue(std::function<void()> job);
        void writerLoop();
        void writeAtomically(const std::string& fileName, const std::string& contents);
        void upgradeOldTradesFile();
        std::string path(const std::string& fileName) const;

        std::string directory;

        // Background writer
        std::mutex mutex;
        std::condition_variable wake;
        std::condition_variable idle;
        std::deque<std::function<void()>> jobs;
        bool busy = false;
        bool stopping = false;
        std::thread writer;
};
