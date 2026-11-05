#include "AlpacaBroker.hpp"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <cstdio>
#include <iostream>
#include <thread>

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
    bool entry = !order.reducesPosition();

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

    // Our id, so Alpaca's order can be matched to the line in data/live/trades.csv
    if (!order.getClientOrderId().empty())
        body["client_order_id"] = order.getClientOrderId();

    // If the strategy set a stop loss and take profit, let Alpaca manage them for us (a "bracket"
    // order). They then trigger on Alpaca's servers even if this program is offline. Good till cancelled,
    // so the stop loss still protects a position held overnight. Entries only: Alpaca rejects brackets
    // that reduce a position.
    if (entry && order.getStopLossPrice() > 0 && order.getTakeProfitPrice() > 0)
    {
        body["order_class"] = "bracket";
        body["time_in_force"] = "gtc";
        body["take_profit"] = {{"limit_price", toPriceText(order.getTakeProfitPrice())}};
        body["stop_loss"] = {{"stop_price", toPriceText(order.getStopLossPrice())}};
    }

    return body;
}

json
AlpacaBroker::buildProtectionJson(const Order& exitOrder)
{
    float remaining = std::fabs(exitOrder.getPositionBefore()) - exitOrder.getQuantity();
    if (!exitOrder.reducesPosition() || remaining <= 1e-6 || exitOrder.getStopLossPrice() <= 0
        || exitOrder.getTakeProfitPrice() <= 0)
        return json();

    // The exit order's levels were set as if it were a new entry its way, e.g. a BUY at 100 has its stop
    // loss at 95 and take profit at 110. The position that remains is the other way round (short), so its
    // stop goes as far above the price (105) and its target as far below (90).
    float price = exitOrder.getPrice();
    float stop = 2 * price - exitOrder.getStopLossPrice();
    float target = 2 * price - exitOrder.getTakeProfitPrice();
    return {
        {"symbol", exitOrder.getTicker()},
        {"qty", toQuantityText(remaining)},
        {"side", exitOrder.isBuy() ? "buy" : "sell"},   // same side as the exit: it closes the rest
        {"type", "limit"},
        {"time_in_force", "gtc"},
        {"order_class", "oco"},
        {"take_profit", {{"limit_price", toPriceText(target)}}},
        {"stop_loss", {{"stop_price", toPriceText(stop)}}}
    };
}

bool
AlpacaBroker::isWorkingStatus(const std::string& status)
{
    return status == "new" || status == "accepted" || status == "pending_new" || status == "partially_filled"
        || status == "accepted_for_bidding" || status == "pending_replace" || status == "pending_cancel"
        || status == "calculated" || status == "replaced";
}

bool
AlpacaBroker::hasPendingOrder(const std::string& ticker)
{
    auto found = pendingOrderIds.find(ticker);
    if (found == pendingOrderIds.end() || dryRun)
        return false;

    HttpResult result = client.get(tradingUrl + "/v2/orders/" + found->second);
    if (!result.ok() || !result.body.is_object())
        return true;   // can't tell: safer to wait than to "correct" a position that's about to change

    if (isWorkingStatus(result.body.value("status", "")))
        return true;
    pendingOrderIds.erase(found);   // filled, cancelled, rejected or expired: the position is settled
    return false;
}

bool
AlpacaBroker::cancelOpenOrders(const std::string& ticker)
{
    std::string listUrl = tradingUrl + "/v2/orders?status=open&limit=500&symbols=" + ticker;
    HttpResult open = client.get(listUrl);
    if (!open.ok() || !open.body.is_array())
        return false;
    if (open.body.empty())
        return true;

    for (const auto& item : open.body)
        client.del(tradingUrl + "/v2/orders/" + item.value("id", ""));
    std::cout << "Cancelled " << open.body.size() << " open " << ticker
              << " order(s) (earlier stop loss / take profit) so the exit can go through" << std::endl;

    // Cancels are usually immediate, but wait up to ~2s for Alpaca to confirm
    for (int attempt = 0; attempt < 10; attempt++)
    {
        HttpResult check = client.get(listUrl);
        if (check.ok() && check.body.is_array() && check.body.empty())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}

// Reduce or close a position: cancel the stop loss / take profit orders holding the shares, send a plain
// order, then re-protect whatever remains
int
AlpacaBroker::sendExit(Order& order, json body)
{
    auto started = std::chrono::steady_clock::now();
    if (!cancelOpenOrders(order.getTicker()))
        std::cerr << "WARNING - " << order.getTicker() << " still has open orders; trying the exit anyway" << std::endl;

    HttpResult result;
    for (int attempt = 1; attempt <= 3; attempt++)
    {
        result = client.post(tradingUrl + "/v2/orders", body);
        // Shares can stay reserved for a moment after a cancel: try again shortly
        bool reserved = result.status == 403 && result.error.find("insufficient qty") != std::string::npos;
        if (!reserved)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    lastReceipt.roundTripMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    if (!result.ok())
    {
        lastReceipt.brokerStatus = "rejected";
        lastReceipt.error = "HTTP " + std::to_string(result.status) + ": " + result.error;
        std::cerr << "ERROR - Alpaca rejected exit order (" << result.status << "): " << result.error
                  << "\n  Order sent: " << body.dump() << std::endl;
        return 0;
    }

    lastReceipt.brokerOrderId = result.body.value("id", "");
    lastReceipt.brokerStatus = result.body.value("status", "");
    pendingOrderIds[order.getTicker()] = lastReceipt.brokerOrderId;
    std::cout << "Exit sent to Alpaca: " << body.value("side", "") << " " << body.value("qty", "") << " "
              << order.getTicker() << " | id " << lastReceipt.brokerOrderId << " | status " << lastReceipt.brokerStatus
              << " | " << std::fixed << std::setprecision(1) << lastReceipt.roundTripMs << " ms" << std::endl;

    json protection = buildProtectionJson(order);
    if (!protection.is_null())
    {
        HttpResult protect = client.post(tradingUrl + "/v2/orders", protection);
        if (protect.ok())
            std::cout << "Re-protected the remaining " << protection.value("qty", "") << " " << order.getTicker()
                      << ": stop " << protection["stop_loss"].value("stop_price", "") << ", target "
                      << protection["take_profit"].value("limit_price", "") << std::endl;
        else
            std::cerr << "WARNING - Could not place the stop loss / take profit for the remaining " << order.getTicker()
                      << " (" << protect.status << "): " << protect.error << std::endl;
    }
    return 1;
}

int
AlpacaBroker::placeOrder(Order order)
{
    json body = buildOrderJson(order);
    lastReceipt = OrderReceipt();
    lastReceipt.orderClass = body.value("order_class", "simple");

    if (dryRun)
    {
        std::cout << "DRY RUN - would send order: " << body.dump() << std::endl;
        lastReceipt.brokerStatus = "dry_run";
        return 1;
    }

    if (order.reducesPosition())
        return sendExit(order, body);

    auto started = std::chrono::steady_clock::now();
    HttpResult result = client.post(tradingUrl + "/v2/orders", body);
    lastReceipt.roundTripMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    if (!result.ok())
    {
        lastReceipt.brokerStatus = "rejected";
        lastReceipt.error = "HTTP " + std::to_string(result.status) + ": " + result.error;
        std::cerr << "ERROR - Alpaca rejected order (" << result.status << "): " << result.error
                  << "\n  Order sent: " << body.dump() << std::endl;
        return 0;
    }

    lastReceipt.brokerOrderId = result.body.value("id", "");
    lastReceipt.brokerStatus = result.body.value("status", "");
    pendingOrderIds[order.getTicker()] = lastReceipt.brokerOrderId;

    std::cout << "Order sent to Alpaca: " << body.value("side", "") << " " << body.value("qty", "")
              << " " << order.getTicker()
              << " | id " << lastReceipt.brokerOrderId
              << " | status " << lastReceipt.brokerStatus
              << " | " << std::fixed << std::setprecision(1) << lastReceipt.roundTripMs << " ms" << std::endl;
    return 1;
}

Position
AlpacaBroker::getLatestPosition(std::string ticker)
{
    lastPositionReliable = true;
    lastPosition = json::object();

    if (dryRun)
        return Position(ticker, 0, 0);

    HttpResult result = client.get(tradingUrl + "/v2/positions/" + ticker);

    if (result.status == 404) // Alpaca returns 404 when we hold none of this ticker
        return Position(ticker, 0, 0);

    if (!result.ok())
    {
        std::cerr << "WARNING - Could not get position for " << ticker << ": " << result.error << std::endl;
        lastPositionReliable = false;   // don't mistake a failed request for "we hold nothing"
        return Position(ticker, 0, 0);
    }
    lastPosition = result.body;

    // Alpaca sends numbers as text, e.g. {"qty":"5","avg_entry_price":"181.20","side":"long"}
    float quantity = std::fabs(std::stof(result.body.value("qty", "0")));
    if (result.body.value("side", "long") == "short")
        quantity = -quantity;

    float avgPrice = std::stof(result.body.value("avg_entry_price", "0"));
    return Position(ticker, quantity, avgPrice);
}

static double numberField(const json& body, const char* key)
{
    // Alpaca sends numbers as text, e.g. "100123.45"
    try { return body.contains(key) && body[key].is_string() ? std::stod(body[key].get<std::string>()) : 0.0; }
    catch (const std::exception&) { return 0.0; }
}

AccountSnapshot
AlpacaBroker::getAccount()
{
    AccountSnapshot snapshot;
    if (dryRun)
        return snapshot;

    HttpResult result = client.get(tradingUrl + "/v2/account");
    if (!result.ok())
        return snapshot;

    snapshot.available = true;
    snapshot.equity = numberField(result.body, "equity");
    snapshot.lastEquity = numberField(result.body, "last_equity");
    snapshot.cash = numberField(result.body, "cash");
    return snapshot;
}
