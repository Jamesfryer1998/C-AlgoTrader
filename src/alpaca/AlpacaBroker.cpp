#include "AlpacaBroker.hpp"
#include "AlpacaApi.hpp"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <cstdio>
#include <iostream>
#include <set>
#include <thread>

namespace
{
    // <data url>/v2/stocks/<SYMBOL>/trades/latest
    const std::string LATEST_TRADE_PATH = "/trades/latest";
    const int HTTP_FORBIDDEN = 403;
    const int HTTP_NOT_FOUND = 404;   // what Alpaca answers for a position we don't hold

    // How long orders stay working
    const std::string DAY_ORDER = "day";
    const std::string GOOD_TILL_CANCELLED = "gtc";   // stop losses must outlive the day to protect overnight

    // Order statuses that mean the order is still working (not filled, cancelled, rejected or expired)
    const std::set<std::string> WORKING_STATUSES = {"new", "accepted", "pending_new", "partially_filled",
                                                    "accepted_for_bidding", "pending_replace", "pending_cancel",
                                                    "calculated", "replaced"};

    // After cancelling orders, check this many times, this far apart, for Alpaca to confirm
    const int CANCEL_CHECKS = 10;
    const std::chrono::milliseconds CANCEL_CHECK_INTERVAL{200};

    // Shares can stay reserved for a moment after a cancel: Alpaca then rejects the exit with this message,
    // so try again shortly
    const std::string SHARES_STILL_RESERVED = "insufficient qty";
    const int EXIT_ATTEMPTS = 3;
    const std::chrono::milliseconds EXIT_RETRY_WAIT{300};

    // Less than this left after an exit counts as nothing (float rounding)
    const float NOTHING_LEFT = 1e-6f;

    const double CENTS_PER_DOLLAR = 100.0;
}

// Alpaca wants prices in cents (2 decimal places) for stocks over $1. Send them as text to avoid
// float noise like 181.2000045.
static std::string toPriceText(float price)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f", std::round(price * CENTS_PER_DOLLAR) / CENTS_PER_DOLLAR);
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
    tradingUrl = alpaca.value("trading_url", AlpacaApi::PAPER_TRADING_URL);
    dataUrl = alpaca.value("data_url", AlpacaApi::DATA_URL);
    feed = alpaca.value("data_feed", AlpacaApi::IEX_FEED);
    dryRun = alpaca.value("dry_run", false) || feed == AlpacaApi::TEST_FEED;
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

    HttpResult result = client.get(tradingUrl + AlpacaApi::ACCOUNT_PATH);
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
    std::string url = dataUrl + AlpacaApi::STOCKS_PATH + ticker + LATEST_TRADE_PATH + "?feed=" + (feed == AlpacaApi::SIP_FEED ? AlpacaApi::SIP_FEED : AlpacaApi::IEX_FEED);
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
        {"time_in_force", DAY_ORDER}
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
        body["time_in_force"] = GOOD_TILL_CANCELLED;
        body["take_profit"] = {{"limit_price", toPriceText(order.getTakeProfitPrice())}};
        body["stop_loss"] = {{"stop_price", toPriceText(order.getStopLossPrice())}};
    }

    return body;
}

json
AlpacaBroker::buildProtectionJson(const Order& exitOrder)
{
    float remaining = std::fabs(exitOrder.getPositionBefore()) - exitOrder.getQuantity();
    if (!exitOrder.reducesPosition() || remaining <= NOTHING_LEFT || exitOrder.getStopLossPrice() <= 0
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
        {"time_in_force", GOOD_TILL_CANCELLED},
        {"order_class", "oco"},
        {"take_profit", {{"limit_price", toPriceText(target)}}},
        {"stop_loss", {{"stop_price", toPriceText(stop)}}}
    };
}

bool
AlpacaBroker::isWorkingStatus(const std::string& status)
{
    return WORKING_STATUSES.count(status) > 0;
}

bool
AlpacaBroker::hasPendingOrder(const std::string& ticker)
{
    auto found = pendingOrderIds.find(ticker);
    if (found == pendingOrderIds.end() || dryRun)
        return false;

    HttpResult result = client.get(tradingUrl + AlpacaApi::ORDERS_PATH + "/" + found->second);
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
    std::string listUrl = tradingUrl + AlpacaApi::ORDERS_PATH + "?status=open&symbols=" + ticker;
    HttpResult open = client.get(listUrl);
    if (!open.ok() || !open.body.is_array())
        return false;
    if (open.body.empty())
        return true;

    for (const auto& item : open.body)
        client.del(tradingUrl + AlpacaApi::ORDERS_PATH + "/" + item.value("id", ""));
    std::cout << "Cancelled " << open.body.size() << " open " << ticker
              << " order(s) (earlier stop loss / take profit) so the exit can go through" << std::endl;

    // Cancels are usually immediate, but give Alpaca a couple of seconds to confirm
    for (int attempt = 0; attempt < CANCEL_CHECKS; attempt++)
    {
        HttpResult check = client.get(listUrl);
        if (check.ok() && check.body.is_array() && check.body.empty())
            return true;
        std::this_thread::sleep_for(CANCEL_CHECK_INTERVAL);
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
    for (int attempt = 1; attempt <= EXIT_ATTEMPTS; attempt++)
    {
        result = client.post(tradingUrl + AlpacaApi::ORDERS_PATH, body);
        // Shares can stay reserved for a moment after a cancel: try again shortly
        bool reserved = result.status == HTTP_FORBIDDEN && result.error.find(SHARES_STILL_RESERVED) != std::string::npos;
        if (!reserved)
            break;
        std::this_thread::sleep_for(EXIT_RETRY_WAIT);
    }
    lastReceipt.roundTripMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    if (!result.ok())
    {
        lastReceipt.brokerStatus = REJECTED;
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
        HttpResult protect = client.post(tradingUrl + AlpacaApi::ORDERS_PATH, protection);
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
        lastReceipt.brokerStatus = DRY_RUN;
        return 1;
    }

    if (order.reducesPosition())
        return sendExit(order, body);

    auto started = std::chrono::steady_clock::now();
    HttpResult result = client.post(tradingUrl + AlpacaApi::ORDERS_PATH, body);
    lastReceipt.roundTripMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    if (!result.ok())
    {
        lastReceipt.brokerStatus = REJECTED;
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

    HttpResult result = client.get(tradingUrl + AlpacaApi::POSITIONS_PATH + "/" + ticker);

    if (result.status == HTTP_NOT_FOUND)
        return Position(ticker, 0, 0);

    if (!result.ok())
    {
        std::cerr << "WARNING - Could not get position for " << ticker << ": " << result.error << std::endl;
        lastPositionReliable = false;   // don't mistake a failed request for "we hold nothing"
        return Position(ticker, 0, 0);
    }
    lastPosition = result.body;

    float quantity = std::fabs(AlpacaApi::numberField(result.body, AlpacaApi::QUANTITY));
    if (result.body.value(AlpacaApi::SIDE, "") == AlpacaApi::SHORT)
        quantity = -quantity;

    float avgPrice = AlpacaApi::numberField(result.body, AlpacaApi::AVERAGE_PRICE);
    return Position(ticker, quantity, avgPrice);
}

AccountSnapshot
AlpacaBroker::getAccount()
{
    AccountSnapshot snapshot;
    if (dryRun)
        return snapshot;

    HttpResult result = client.get(tradingUrl + AlpacaApi::ACCOUNT_PATH);
    if (!result.ok())
        return snapshot;

    snapshot.available = true;
    snapshot.equity = AlpacaApi::numberField(result.body, "equity");
    snapshot.lastEquity = AlpacaApi::numberField(result.body, "last_equity");
    snapshot.cash = AlpacaApi::numberField(result.body, "cash");
    return snapshot;
}
