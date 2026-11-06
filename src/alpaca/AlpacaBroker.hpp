#pragma once

#include <ctime>

#include <map>
#include <string>
#include <nlohmann/json.hpp>
#include "AlpacaClient.hpp"
#include "../broker/AccountSnapshot.hpp"
#include "../broker/BrokerBase.hpp"

using json = nlohmann::json;

/**
 * AlpacaBroker
 *
 * Sends orders to Alpaca over its REST API. Nothing needs to be running on your computer
 * apart from this program.
 *
 * Config (src/config/config.json -> "alpaca"):
 *   "trading_url": "https://paper-api.alpaca.markets"  -> paper trading (fake money)
 *                  "https://api.alpaca.markets"        -> live trading (real money)
 *   "dry_run": true  -> print the order instead of sending it
 *
 * The "test" data feed always runs as a dry run, because its symbol (FAKEPACA) can't be traded.
 */
class AlpacaBroker : public BrokerBase
{
    public:
        AlpacaBroker(const json& config);

        int connect() override;
        int disconnect() override;
        float getLatestPrice(std::string ticker) override;
        int placeOrder(Order order) override;
        Position getLatestPosition(std::string ticker) override;

        bool isDryRun() const { return dryRun; }

        // Asks Alpaca whether our last order for this ticker is still waiting to fill
        bool hasPendingOrder(const std::string& ticker) override;

        // Alpaca's own figures, for display: real fills and latest prices (our PerformanceTracker is an
        // estimate from bar closes)
        AccountSnapshot getAccount();

        // Alpaca's market clock (knows holidays and early closes). known = false if it couldn't be read.
        struct MarketClock
        {
            bool known = false;
            bool isOpen = false;
            std::time_t nextOpen = 0;
            std::time_t nextClose = 0;
        };
        MarketClock getMarketClock();

        // Alpaca's position details from the last getLatestPosition() call
        // (qty, avg_entry_price, current_price, market_value, unrealized_pl, unrealized_plpc, ...);
        // empty object when flat
        const json& lastPositionDetails() const { return lastPosition; }
        bool reportsRealPositions() const override { return !dryRun; }

        // The JSON body we POST to /v2/orders (public so it can be unit tested).
        // An entry with a stop loss and take profit becomes a "bracket" order (Alpaca manages the exits on its
        // servers). An order that reduces or closes a position is sent as a plain order: Alpaca only accepts
        // brackets for entries.
        static json buildOrderJson(const Order& order);   // includes client_order_id if the order has one

        // Protection for what's left after a partial exit: a stop loss and take profit on the remaining shares
        // as one "oco" order (one cancels the other). The levels keep the same distances from the price as the
        // exit order's own stop loss / take profit, mirrored to the side of the position that remains.
        // Empty if nothing remains.
        static json buildProtectionJson(const Order& exitOrder);

        // Alpaca statuses that mean an order is still working (not filled, cancelled, rejected or expired)
        static bool isWorkingStatus(const std::string& status);

    private:
        AlpacaClient client;
        std::string tradingUrl;
        std::string dataUrl;
        std::string feed;
        bool dryRun;
        json lastPosition = json::object();
        std::map<std::string, std::string> pendingOrderIds;   // ticker -> our latest order's Alpaca id

        // Cancel every open order for a ticker (e.g. the stop loss / take profit legs of earlier brackets) and
        // wait briefly until Alpaca confirms. Returns false if some are still open.
        bool cancelOpenOrders(const std::string& ticker);
        int sendExit(Order& order, json body);
};
