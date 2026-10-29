#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "AlpacaClient.hpp"
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
        bool reportsRealPositions() const override { return !dryRun; }

        // The JSON body we POST to /v2/orders (public so it can be unit tested)
        static json buildOrderJson(const Order& order);

    private:
        AlpacaClient client;
        std::string tradingUrl;
        std::string dataUrl;
        std::string feed;
        bool dryRun;
};
