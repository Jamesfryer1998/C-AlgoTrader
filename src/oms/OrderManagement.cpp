#include <algorithm>
#include <cmath>

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

void OrderManagement::onNewOrder(Order& order)
{
    // Wait for new order to be added to orders from strat engine
    if (marketData == nullptr)
    {
        std::cerr << "ERROR - OMS has no market data, call setMarketData first" << std::endl;
        return;
    }

    bool orderValid = validator.validateOrder(order, *marketData, positions);
    if(orderValid)
    {
        addOrder(order);

        // connect with Broker API to place order here
        if (broker->placeOrder(order))
        {
            recordTrade(order);
            syncPosition(order.getTicker());
        }
    }
}

// Ask the broker what we actually hold for this ticker and make that our one position for it.
// Called after each order, and at start-up so positions from a previous run are not forgotten.
void OrderManagement::syncPosition(const string& ticker)
{
    Position latest = broker->getLatestPosition(ticker);

    std::erase_if(positions, [&ticker](Position& position) { return position.getTicker() == ticker; });

    if (latest.getQuantity() != 0)
        addPosition(latest);

    reconcileWithBroker(ticker, latest.getQuantity());

    if (store)
        store->savePositions(positions);
}

void OrderManagement::enablePersistence(TradeStore* tradeStore, PerformanceTracker* performanceTracker)
{
    store = tradeStore;
    tracker = performanceTracker;
    if (!store)
        return;

    // Replay every saved trade so order ids and the P&L book carry on from the last run
    std::vector<TradeRecord> trades = store->loadTrades();
    for (const auto& trade : trades)
    {
        Order order(stringToOrderType(trade.type), trade.ticker, trade.quantity, trade.price);
        addOrder(order);
        if (tracker)
            tracker->recordFill(trade.ticker, trade.isBuy(), trade.quantity, trade.price);
    }

    // Last known positions (the broker's view replaces these when syncPosition runs)
    for (Position position : store->loadPositions())
        addPosition(position);

    std::cout << "Restored " << trades.size() << " trade(s) and " << positions.size()
              << " position(s) from " << store->getDirectory() << std::endl;
}

void OrderManagement::recordTrade(const Order& order)
{
    TradeRecord trade;
    trade.time = (marketData && !marketData->getData().empty()) ? marketData->getCurrentData().DateTime : "";
    trade.ticker = order.getTicker();
    trade.type = order.getTypeAsString();
    trade.quantity = order.getQuantity();
    trade.price = order.getPrice();
    trade.stopLoss = order.getStopLossPrice();
    trade.takeProfit = order.getTakeProfitPrice();

    if (store)
        store->appendTrade(trade);
    if (tracker)
        tracker->recordFill(trade.ticker, trade.isBuy(), trade.quantity, trade.price);
}

// Alpaca fills stop losses and take profits on its own servers, so our book can fall behind.
// If the broker holds a different amount than we think, record the difference as a trade at the
// latest price so P&L and the saved history stay in step with reality.
void OrderManagement::reconcileWithBroker(const string& ticker, float brokerQuantity)
{
    if (!tracker || !broker->reportsRealPositions() || !marketData || marketData->getData().empty())
        return;

    double difference = brokerQuantity - tracker->getPositionQuantity(ticker);
    if (std::fabs(difference) < 1e-6)
        return;

    float price = marketData->getLastClosePrice();
    Order adjustment(difference > 0 ? OrderType::BUY : OrderType::SELL, ticker, std::fabs(difference), price);
    std::cout << "Position adjusted to match the broker (" << ticker << " " << tracker->getPositionQuantity(ticker)
              << " -> " << brokerQuantity << ", e.g. a stop loss or take profit filled) at ~$" << price << std::endl;
    recordTrade(adjustment);
}
