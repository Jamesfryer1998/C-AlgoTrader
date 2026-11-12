#pragma once

#include <ctime>

#include <map>
#include <memory>
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
 * Config (config/general.json -> "alpaca"):
 *   "trading_url": "https://paper-api.alpaca.markets"  -> paper trading (fake money)
 *                  "https://api.alpaca.markets"        -> live trading (real money)
 *   "dry_run": true  -> print the order instead of sending it
 *
 * The "test" data feed always runs as a dry run, because its symbol (FAKEPACA) can't be traded.
 */
class AlpacaBroker : public BrokerBase
{
    public:
        // client: how it talks to Alpaca (none: a real AlpacaClient; tests pass a fake)
        explicit AlpacaBroker(const json& config, std::shared_ptr<AlpacaClient> client = nullptr);

        int connect() override;
        int disconnect() override;
        float getLatestPrice(std::string ticker) override;
        int placeOrder(Order order) override;
        Position getLatestPosition(std::string ticker) override;

        bool isDryRun() const { return dryRun; }
        bool isPaper() const;
        const std::string& getTradingUrl() const { return tradingUrl; }

        // The account as connect() found it, for the start-up summary
        struct AccountStatus
        {
            std::string status;   // e.g. ACTIVE
            double cash = 0;
            double buyingPower = 0;
        };
        const AccountStatus& getConnectedAccount() const { return connectedAccount; }

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

        // A cheap request when the connection to Alpaca has been idle a while, so it stays open for the next order
        void keepConnectionWarm();

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
        std::shared_ptr<AlpacaClient> client;
        std::string tradingUrl;
        std::string dataUrl;
        std::string feed;
        bool dryRun;
        AccountStatus connectedAccount;
        json lastPosition = json::object();
        std::map<std::string, std::string> pendingOrderIds;   // ticker -> our latest order's Alpaca id
        // ticker -> the ids of the stop loss / take profit orders holding its shares, from the replies to the
        // orders that placed them (a bracket entry's legs, a re-protection). Unknown after a restart.
        std::map<std::string, std::vector<std::string>> protectionIds;

        // Cancel the stop loss / take profit orders holding a ticker's shares, so an exit can sell them: by the
        // ids we know, or (unknown, or lookUp) every open order for the ticker. Doesn't wait for Alpaca to
        // confirm: the exit is retried while the shares are still reserved.
        void cancelProtection(const std::string& ticker, bool lookUp = false);
        void rememberProtection(const std::string& ticker, const json& reply);
        int sendExit(Order& order, json body);
        HttpResult counted(HttpResult result);   // one more request to this order, and its connect time
};
