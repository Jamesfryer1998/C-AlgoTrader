#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "OrderManagement.hpp"

OrderManagement::OrderManagement() 
{
}

OrderManagement::~OrderManagement() 
{
}

void
OrderManagement::addOrder(Order &order)
{
    order.setId(latestOrderId);
    orders.push_back(order);
    latestOrderId++;
}

void
OrderManagement::addPosition(Position &position)
{
    position.setId(latestPositionId);
    positions.push_back(position);
    latestPositionId++;
}

void OrderManagement::removeOrder(int id)
{
    std::erase_if(orders, [id](Order& order) { return order.getId() == id; });
}

void OrderManagement::removePosition(int id)
{
    std::erase_if(positions, [id](Position& position) { return position.getId() == id; });
}

// Wall-clock time now, e.g. 2026-10-29T18:31:00.123Z
static std::string utcNowMillis()
{
    auto now = std::chrono::system_clock::now();
    std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    int millis = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
    gmtime_r(&seconds, &tm);
    char text[64];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, millis);
    return text;
}

void OrderManagement::onNewOrder(Order& order)
{
    if (marketData == nullptr)
    {
        std::cerr << "ERROR - OMS has no market data, call setMarketData first" << std::endl;
        return;
    }

    // Every order the strategies produce gets the next number, whether or not it is sent
    int ourId = latestOrderId;

    if (!validator.validateOrder(order, *marketData, positions))
    {
        latestOrderId++;
        if (store)
        {
            std::cout << "Order " << ourId << " (" << order.getTypeAsString() << " " << order.getQuantity()
                      << " " << order.getTicker() << ") blocked by risk checks: " << validator.lastFailure << std::endl;
            TradeRecord trade = makeRecord(order, ourId);
            trade.status = "blocked";
            trade.note = validator.lastFailure;
            saveTrade(trade);
        }
        return;
    }

    // Live: tell the broker our id so its order can be matched to ours (unique across restarts)
    if (store)
        order.setClientOrderId("algo-" + std::to_string(ourId) + "-" + std::to_string(std::time(nullptr)));

    addOrder(order);   // takes ourId
    std::string sentAt = utcNowMillis();

    // ---- The order goes out here. Nothing before this line touches the disk or the network. ----
    bool accepted = broker->placeOrder(order);

    // Everything below happens after the order has been sent
    const BrokerBase::OrderReceipt& receipt = broker->lastReceipt;
    TradeRecord trade = makeRecord(order, ourId);
    trade.sentAtUtc = sentAt;
    trade.orderClass = receipt.orderClass;
    trade.brokerOrderId = receipt.brokerOrderId;
    trade.brokerStatus = receipt.brokerStatus;
    trade.roundTripMs = receipt.roundTripMs;
    trade.note = receipt.error;
    trade.status = !accepted ? "rejected" : (receipt.brokerStatus == "dry_run" ? "dry_run" : "sent");
    saveTrade(trade);

    if (accepted)
        syncPosition(order.getTicker());
}

// Ask the broker what we actually hold for this ticker and make that our one position for it.
// Called after each order, and at start-up so positions from a previous run are not forgotten.
void OrderManagement::syncPosition(const string& ticker)
{
    Position latest = broker->getLatestPosition(ticker);
    if (!broker->lastPositionReliable)
        return;   // the broker couldn't be asked; keep what we have and try again next bar

    std::erase_if(positions, [&ticker](Position& position) { return position.getTicker() == ticker; });

    if (latest.getQuantity() != 0)
        addPosition(latest);

    reconcileWithBroker(ticker, latest);

    if (store)
        store->savePositions(positions);
}

void OrderManagement::enablePersistence(TradeStore* tradeStore, PerformanceTracker* performanceTracker)
{
    store = tradeStore;
    tracker = performanceTracker;
    if (!store)
        return;

    // Replay saved trades so order numbers and the P&L book carry on from the last run
    std::vector<TradeRecord> trades = store->loadTrades();
    int replayed = 0;
    for (const auto& trade : trades)
    {
        latestOrderId = std::max(latestOrderId, trade.ourId + 1);
        if (!trade.changedPosition())
            continue;   // blocked / rejected orders never changed anything

        Order order(stringToOrderType(trade.type), trade.ticker, trade.quantity, trade.price);
        order.setId(trade.ourId);
        orders.push_back(order);
        if (tracker)
            tracker->recordFill(trade.ticker, trade.isBuy(), trade.quantity, trade.price);
        replayed++;
    }

    // Last known positions (the broker's view replaces these when syncPosition runs)
    for (Position position : store->loadPositions())
        addPosition(position);

    std::cout << "Restored " << trades.size() << " order record(s) (" << replayed << " that changed a position) and "
              << positions.size() << " position(s) from " << store->getDirectory()
              << ". Next order id: " << latestOrderId << std::endl;
}

TradeRecord OrderManagement::makeRecord(const Order& order, int ourId) const
{
    TradeRecord trade;
    trade.ourId = ourId;
    trade.time = (marketData && !marketData->getData().empty()) ? marketData->getCurrentData().DateTime : "";
    trade.strategy = order.getStrategy();
    trade.ticker = order.getTicker();
    trade.side = order.isBuy() ? "buy" : "sell";
    trade.type = order.getTypeAsString();
    trade.quantity = order.getQuantity();
    trade.price = order.getPrice();
    trade.stopLoss = order.getStopLossPrice();
    trade.takeProfit = order.getTakeProfitPrice();
    trade.clientOrderId = order.getClientOrderId();
    return trade;
}

// Queue the line for trades.csv (written on the store's background thread) and update the P&L book
void OrderManagement::saveTrade(const TradeRecord& trade)
{
    if (store)
        store->appendTrade(trade);
    if (tracker && trade.changedPosition())
        tracker->recordFill(trade.ticker, trade.isBuy(), trade.quantity, trade.price);
}

// Alpaca fills stop losses and take profits on its own servers, so our book can fall behind.
// If the broker holds a different amount than we think, record the difference as a trade at the
// latest price so P&L and the saved history stay in step with reality.
void OrderManagement::reconcileWithBroker(const string& ticker, const Position& brokerPosition)
{
    float brokerQuantity = brokerPosition.getQuantity();
    if (!tracker || !broker->reportsRealPositions() || !marketData || marketData->getData().empty())
        return;

    double ours = tracker->getPositionQuantity(ticker);
    double difference = brokerQuantity - ours;
    if (std::fabs(difference) < 1e-6)
        return;

    // If we're taking over a whole position we never traded ourselves (e.g. bought before records
    // were kept), use the broker's real average cost so our P&L matches the broker's. Otherwise the
    // difference was filled just now (e.g. a stop loss), so the latest close is the best estimate.
    bool adoptingWholePosition = std::fabs(ours) < 1e-6 && brokerPosition.getAvgPrice() > 0;
    float price = adoptingWholePosition ? brokerPosition.getAvgPrice() : marketData->getLastClosePrice();
    Order adjustment(difference > 0 ? OrderType::BUY : OrderType::SELL, ticker, std::fabs(difference), price);
    adjustment.setStrategy("broker");

    TradeRecord trade = makeRecord(adjustment, latestOrderId++);
    trade.sentAtUtc = utcNowMillis();
    trade.status = "adjustment";
    char quantities[96];
    std::snprintf(quantities, sizeof(quantities), "broker holds %g but our records had %g", brokerQuantity, ours);
    trade.note = std::string(quantities)
               + " (e.g. a stop loss / take profit filled at the broker, or trades from before records were kept)";

    std::cout << "Position adjusted to match the broker (" << ticker << " " << ours << " -> " << brokerQuantity
              << ") at ~$" << price << std::endl;
    saveTrade(trade);
}
