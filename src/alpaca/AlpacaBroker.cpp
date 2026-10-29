#include "AlpacaBroker.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>

// Alpaca wants prices in cents (2 decimal places) for stocks over $1. Send them as text to avoid
// float noise like 181.2000045.
static std::string toPriceText(float price)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f", std::round(price * 100.0) / 100.0);
    return text;
}

static std::string toQuantityText(float quantity)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%g", quantity);
    return text;
}

AlpacaBroker::AlpacaBroker(const json& config)
{
    brokerName = "Alpaca";

    json alpaca = config.value("alpaca", json::object());
    tradingUrl = alpaca.value("trading_url", "https://paper-api.alpaca.markets");
    dataUrl = alpaca.value("data_url", "https://data.alpaca.markets");
    feed = alpaca.value("data_feed", "iex");
    dryRun = alpaca.value("dry_run", false) || feed == "test";
}

int
AlpacaBroker::connect()
{
    if (!client.hasKeys())
    {
        std::cerr << "ERROR - Alpaca keys missing. In the project root run:\n"
                  << "  cp .env.example .env\n"
                  << "then put your paper trading keys from alpaca.markets into .env\n"
                  << "(run the app from the project root so it can find .env)" << std::endl;
        return 0;
    }

    HttpResult result = client.get(tradingUrl + "/v2/account");
    if (!result.ok())
    {
        std::cerr << "ERROR - Could not reach Alpaca account at " << tradingUrl
                  << " (" << result.status << "): " << result.error << std::endl;
        return 0;
    }

    std::cout << "Connected to Alpaca (" << tradingUrl << ")"
              << "\n  - Account status: " << result.body.value("status", "?")
              << "\n  - Cash:           $" << result.body.value("cash", "?")
              << "\n  - Buying power:   $" << result.body.value("buying_power", "?")
              << (dryRun ? "\n  - DRY RUN: orders will be printed, not sent" : "")
              << std::endl;
    return 1;
}

int
AlpacaBroker::disconnect()
{
    return 1; // REST is stateless, nothing to close
}

float
AlpacaBroker::getLatestPrice(std::string ticker)
{
    std::string url = dataUrl + "/v2/stocks/" + ticker + "/trades/latest?feed=" + (feed == "sip" ? "sip" : "iex");
    HttpResult result = client.get(url);

    if (!result.ok() || !result.body.contains("trade"))
    {
        std::cerr << "WARNING - Could not get latest price for " << ticker << ": " << result.error << std::endl;
        return 0.0f;
    }
    return result.body["trade"].value("p", 0.0f);
}

json
AlpacaBroker::buildOrderJson(const Order& order)
{
    json body = {
        {"symbol", order.getTicker()},
        {"qty", toQuantityText(order.getQuantity())},
        {"side", order.isBuy() ? "buy" : "sell"},
        {"time_in_force", "day"}
    };

    if (order.isLimit())
    {
        body["type"] = "limit";
        body["limit_price"] = toPriceText(order.getPrice());
    }
    else if (order.isStop())
    {
        body["type"] = "stop";
        body["stop_price"] = toPriceText(order.getPrice());
    }
    else
    {
        body["type"] = "market";
    }

    // If the strategy set a stop loss and take profit, let Alpaca manage them for us (a "bracket"
    // order). They then trigger on Alpaca's servers even if this program is offline.
    if (order.getStopLossPrice() > 0 && order.getTakeProfitPrice() > 0)
    {
        body["order_class"] = "bracket";
        body["take_profit"] = {{"limit_price", toPriceText(order.getTakeProfitPrice())}};
        body["stop_loss"] = {{"stop_price", toPriceText(order.getStopLossPrice())}};
    }

    return body;
}

int
AlpacaBroker::placeOrder(Order order)
{
    json body = buildOrderJson(order);

    if (dryRun)
    {
        std::cout << "DRY RUN - would send order: " << body.dump() << std::endl;
        return 1;
    }

    HttpResult result = client.post(tradingUrl + "/v2/orders", body);
    if (!result.ok())
    {
        std::cerr << "ERROR - Alpaca rejected order (" << result.status << "): " << result.error
                  << "\n  Order sent: " << body.dump() << std::endl;
        return 0;
    }

    std::cout << "Order sent to Alpaca: " << body.value("side", "") << " " << body.value("qty", "")
              << " " << order.getTicker()
              << " | id " << result.body.value("id", "?")
              << " | status " << result.body.value("status", "?") << std::endl;
    return 1;
}

Position
AlpacaBroker::getLatestPosition(std::string ticker)
{
    if (dryRun)
        return Position(ticker, 0, 0);

    HttpResult result = client.get(tradingUrl + "/v2/positions/" + ticker);

    if (result.status == 404) // Alpaca returns 404 when we hold none of this ticker
        return Position(ticker, 0, 0);

    if (!result.ok())
    {
        std::cerr << "WARNING - Could not get position for " << ticker << ": " << result.error << std::endl;
        return Position(ticker, 0, 0);
    }

    // Alpaca sends numbers as text, e.g. {"qty":"5","avg_entry_price":"181.20","side":"long"}
    float quantity = std::fabs(std::stof(result.body.value("qty", "0")));
    if (result.body.value("side", "long") == "short")
        quantity = -quantity;

    float avgPrice = std::stof(result.body.value("avg_entry_price", "0"));
    return Position(ticker, quantity, avgPrice);
}
