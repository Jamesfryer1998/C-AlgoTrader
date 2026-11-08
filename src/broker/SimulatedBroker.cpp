#include "SimulatedBroker.hpp"
#include "../util/Log.hpp"
#include <algorithm>
#include <random>
#include <cmath>
#include "../util/Defaults.hpp"

namespace
{
    const std::string BROKER_NAME = "Simulated";

    const unsigned DEFAULT_RANDOM_SEED = 42;    // for repeatable random slippage in tests

    // What a simulated order looks like to the rest of the app
    const std::string ORDER_ID_PREFIX = "sim-";
    const std::string ACCEPTED = "accepted";
    const std::string SIMPLE_ORDER = "simple";

    const char* const STOP_LOSS = "stop loss";
    const char* const TAKE_PROFIT = "take profit";

    const double PERCENT = 100.0;
}

SimulatedBroker::SimulatedBroker(MarketData& marketdata)
: marketData(marketdata)
{
    brokerName = BROKER_NAME;
    
    // Default values
    startingCapital = Defaults::STARTING_CAPITAL;
    currentCash = startingCapital;
    currentEquity = startingCapital;
    highestEquity = startingCapital;
    slippagePercentage = Defaults::SLIPPAGE;
    commissionPerTrade = Defaults::COMMISSION_PER_TRADE;
    totalTrades = 0;
    step = 0;
    detailedLogging = false; // Detailed logging disabled by default
    
    // Random seed initialization
    useFixedSeed = false;
    randomSeed = DEFAULT_RANDOM_SEED;
    
    // Log the default settings
    detailLog() << "SimulatedBroker initialized with:"
              << "\n  - Starting capital: $" << startingCapital
              << "\n  - Commission: $" << commissionPerTrade << " per trade"
              << "\n  - Slippage: ±" << (slippagePercentage * PERCENT) << "% (random variation)"
              << std::endl;
    
    connect();
}

void SimulatedBroker::enableFixedRandomSeed(unsigned int seed)
{
    useFixedSeed = true;
    randomSeed = seed;
    Log::out() << "Using fixed random seed: " << seed << " for deterministic testing" << std::endl;
}

void SimulatedBroker::enableDetailedLogging(bool enable)
{
    detailedLogging = enable;
    Log::out() << "SimulatedBroker: Detailed logging " << (enable ? "enabled" : "disabled") << std::endl;
}

SimulatedBroker::~SimulatedBroker()
{
    disconnect();
}

void
SimulatedBroker::process()
{
    // Check for valid market data
    const auto& data = marketData.getData();
    if(data.size() == 0)
    {
        throw std::runtime_error("No market data found");
    }

    // Always refresh the current condition to ensure we're using the latest data
    // This is critical for timestamp consistency between strategy signals and order execution
    if (useLatestBar)
    {
        currentCondition = data.back();
    }
    else
    {
        if (step >= static_cast<int>(data.size()))
        {
            throw std::runtime_error("SimulatedBroker: no market data for step " + std::to_string(step)
                                     + " (only " + std::to_string(data.size()) + " bars)");
        }
        currentCondition = data[step];
    }
    simulationTime = currentCondition.DateTime;
    latestPrices[currentCondition.Ticker] = currentCondition.Close;
    
    // Log the current time step being processed
    detailLog() << "SimulatedBroker processing time step: " << simulationTime << std::endl;
    
    // Process pending orders with latest market data
    processOrders();
    
    // Check stop losses and take profits
    checkStopLosses();
    checkTakeProfits();
    
    // Update portfolio value
    updatePortfolioValue();
}

int
SimulatedBroker::connect()
{
    Log::detail() << "Connected to simulated broker with $" << startingCapital << " capital" << std::endl;
    return 1;
}

int
SimulatedBroker::disconnect()
{
    detailLog() << "Disconnected from simulated broker" << std::endl;
    detailLog() << "Final equity: $" << currentEquity << std::endl;
    detailLog() << "Total P&L: $" << (currentEquity - startingCapital) << std::endl;
    detailLog() << "Total trades: " << totalTrades << std::endl;
    return 1;
}

float 
SimulatedBroker::getLatestPrice(std::string ticker)
{
    // Check if ticker matches our current condition
    if (ticker == currentCondition.Ticker) {
        return currentCondition.Close;
    }

    // Several stocks (the live app's backtest mode): each stock's last price
    auto known = latestPrices.find(ticker);
    if (known != latestPrices.end())
        return known->second;
    
    // If we had multiple tickers, we'd need more complex logic here
    std::cerr << "Warning: Requested price for ticker " << ticker 
              << " but current data is for " << currentCondition.Ticker << std::endl;
    return currentCondition.Close;
}

int 
SimulatedBroker::placeOrder(Order order)
{
    // Add order to pending orders queue
    pendingOrders.push_back(order);
    lastReceipt = OrderReceipt();
    lastReceipt.brokerOrderId = ORDER_ID_PREFIX + std::to_string(pendingOrders.size() + filledOrders.size());
    lastReceipt.brokerStatus = ACCEPTED;
    lastReceipt.orderClass = SIMPLE_ORDER;
    
    // Update to ensure we're using the most current timestamp from MarketData
    // Get fresh current data from MarketData to ensure timestamp consistency
    currentCondition = marketData.getCurrentData();
    simulationTime = currentCondition.DateTime;
    
    detailLog() << "Order placed for order " 
              << order.getId() << " for " 
              << order.getTypeAsString() << " with " 
              << order.getQuantity() << " shares of " 
              << order.getTicker() << " at " 
              << simulationTime << std::endl;
    return 1;
}

void
SimulatedBroker::processOrders()
{
    if (pendingOrders.empty()) {
        return;
    }
    
    // Process all pending orders with current market data
    std::vector<Order> remainingOrders;
    
    for (auto& order : pendingOrders) {
        // Keep orders that can't be filled yet (e.g. a limit price not reached, or another stock's order
        // waiting for that stock's bar) for the next bar
        if ((severalStocks && order.getTicker() != currentCondition.Ticker) || !checkOrderValidity(order)
            || !executeOrder(order)) {
            remainingOrders.push_back(order);
        }
    }
    
    // Update pending orders with those that weren't processed
    pendingOrders = remainingOrders;
}

bool
SimulatedBroker::checkOrderValidity(const Order& order) const
{
    // For a real system, we'd perform more validation:
    // - Check if we have enough cash for buy orders
    // - Check if we have enough shares for sell orders
    // - Check if order parameters are valid
    // - Check market hours, etc.
    
    return true;
}

bool
SimulatedBroker::executeOrder(Order& order)
{
    // Get current price for the ticker
    float basePrice = getLatestPrice(order.getTicker());
    float originalOrderPrice = order.getPrice();
    
    double slippageMultiplier = 1.0;
    double actualSlippagePercent = 0.0;
    
    if (slippagePercentage > 0.0) {
        if (slippageMode == SlippageMode::Adverse) {
            actualSlippagePercent = order.isBuy() ? slippagePercentage : -slippagePercentage;
        } else {
            std::mt19937 gen;
            if (useFixedSeed) {
                gen.seed(randomSeed);       // fixed seed for tests
            } else {
                std::random_device rd;      // random seed otherwise
                gen.seed(rd());
            }
            std::uniform_real_distribution<> slippageDist(-slippagePercentage, slippagePercentage);
            actualSlippagePercent = slippageDist(gen);
        }
        slippageMultiplier = 1.0 + actualSlippagePercent;
    }
    
    double executionPrice = basePrice * slippageMultiplier;
    
    // For limit orders, check price constraints
    if (order.getType() == OrderType::LIMIT_BUY && executionPrice > order.getPrice()) {
        // Cannot execute buy limit order above limit price
        Log::out() << "Limit Buy order not executed: Market price $" << executionPrice 
                  << " above limit price $" << order.getPrice() << std::endl;
        return false;
    } else if (order.getType() == OrderType::LIMIT_SELL && executionPrice < order.getPrice()) {
        // Cannot execute sell limit order below limit price
        Log::out() << "Limit Sell order not executed: Market price $" << executionPrice 
                  << " below limit price $" << order.getPrice() << std::endl;
        return false;
    }
    
    // Apply commission cost
    currentCash -= commissionPerTrade;
    
    // Update positions based on order
    updatePositions(order, executionPrice);
    updateExitLevels(order);
    
    // Add to filled orders
    order.setPrice(executionPrice); // Update with actual execution price
    filledOrders.push_back(order);
    totalTrades++;
    
    Log::out() << "Order executed: " << (order.isBuy() ? "BUY" : "SELL")
              << " " << order.getQuantity() << " shares of " << order.getTicker()
              << " at $" << std::fixed << std::setprecision(2) << executionPrice;
    
    // Display slippage information
    if (slippagePercentage > 0.0) {
        Log::out() << " (Order price: $" << std::fixed << std::setprecision(2) << originalOrderPrice
                  << ", Slippage: " << (actualSlippagePercent >= 0 ? "+" : "")
                  << std::fixed << std::setprecision(3) << (actualSlippagePercent * PERCENT) << "%)";
    }
    
    Log::out() << std::endl;
    return true;
}

void
SimulatedBroker::updatePositions(const Order& order, double executionPrice)
{
    std::string ticker = order.getTicker();
    double quantity = order.getQuantity();
    double orderTotal = quantity * executionPrice;
    
    // Handle buys and sells differently using the helper method
    bool isBuy = order.isBuy();
    
    if (isBuy) {
        // Subtract cost from cash
        currentCash -= orderTotal;
        
        // Add to position or create new position
        if (positionsByTicker.find(ticker) != positionsByTicker.end()) {
            // Existing position - update it
            Position& existingPos = positionsByTicker[ticker];
            double existingQuantity = existingPos.getQuantity();
            
            // If there's a short position (negative quantity), handle buying to cover
            if (existingQuantity < 0) {
                // Buying to cover a short position
                double newTotalShares = existingQuantity + quantity;
                
                if (newTotalShares >= 0) {
                    // We're covering the entire short position and potentially going long
                    if (newTotalShares > 0) {
                        // Long position after covering short
                        existingPos.setQuantity(newTotalShares);
                        existingPos.setAvgPrice(executionPrice); // Reset average price as we're now long
                        detailLog() << "Covered short position for " << ticker << " and established long position of " 
                                  << newTotalShares << " shares" << std::endl;
                    } else {
                        // Exactly covered the short position, close position
                        positionHistory.push_back(existingPos);
                        positionsByTicker.erase(ticker);
                        detailLog() << "Completely covered short position for " << ticker << std::endl;
                    }
                } else {
                    // Partially covering short position
                    existingPos.setQuantity(newTotalShares);
                    // We don't update avg price for partial short covers
                    detailLog() << "Partially covered short position for " << ticker 
                              << ", remaining short: " << -newTotalShares << " shares" << std::endl;
                }
            } else {
                // Normal buying to increase long position
                double newTotalShares = existingQuantity + quantity;
                double newAvgPrice = ((existingQuantity * existingPos.getAvgPrice()) + 
                                     (quantity * executionPrice)) / newTotalShares;
                
                // Update position
                existingPos.setQuantity(newTotalShares);
                existingPos.setAvgPrice(newAvgPrice);
                detailLog() << "Increased long position for " << ticker << " to " 
                          << newTotalShares << " shares at avg price $" 
                          << std::fixed << std::setprecision(2) << newAvgPrice << std::endl;
            }
        } else {
            // New position
            Position newPosition(ticker, quantity, executionPrice);
            positionsByTicker[ticker] = newPosition;
            detailLog() << "Established new long position for " << ticker << " with " 
                      << quantity << " shares at $" << executionPrice << std::endl;
        }
    } else {
        // Selling
        currentCash += orderTotal;
        
        if (positionsByTicker.find(ticker) != positionsByTicker.end()) {
            // We have an existing position for this ticker
            Position& existingPos = positionsByTicker[ticker];
            double existingQuantity = existingPos.getQuantity();
            
            if (existingQuantity > 0) {
                // Currently long, selling to reduce or close position
                double newTotalShares = existingQuantity - quantity;
                
                if (newTotalShares <= 0) {
                    // Closing long position and potentially going short
                    if (newTotalShares < 0) {
                        // Going short after closing long
                        existingPos.setQuantity(newTotalShares);
                        existingPos.setAvgPrice(executionPrice); // Reset average price as we're now short
                        detailLog() << "Closed long position for " << ticker 
                                  << " and established short position of " 
                                  << -newTotalShares << " shares" << std::endl;
                    } else {
                        // Exactly closed the long position
                        positionHistory.push_back(existingPos);
                        positionsByTicker.erase(ticker);
                        detailLog() << "Completely closed long position for " << ticker << std::endl;
                    }
                } else {
                    // Partially reducing long position
                    existingPos.setQuantity(newTotalShares);
                    // Average price remains unchanged when reducing a long position
                    detailLog() << "Partially closed long position for " << ticker 
                              << ", remaining: " << newTotalShares << " shares" << std::endl;
                }
            } else {
                // Currently short, selling to increase short position
                double newTotalShares = existingQuantity - quantity;
                // The average price is weighted for short positions too
                double newAvgPrice = (existingPos.getAvgPrice() + executionPrice) / 2;
                
                existingPos.setQuantity(newTotalShares);
                existingPos.setAvgPrice(newAvgPrice);
                detailLog() << "Increased short position for " << ticker << " to " 
                          << -newTotalShares << " shares at avg price $" 
                          << std::fixed << std::setprecision(2) << newAvgPrice << std::endl;
            }
        } else {
            // No existing position, establishing a new short position
            Position newPosition(ticker, -quantity, executionPrice);
            positionsByTicker[ticker] = newPosition;
            detailLog() << "Established new short position for " << ticker << " with " 
                      << quantity << " shares at $" << executionPrice << std::endl;
        }
    }
    
    // Update portfolio value after position change
    updatePortfolioValue();
}

void 
SimulatedBroker::updatePortfolioValue()
{
    // Calculate portfolio value: cash + value of all positions
    double portfolioValue = currentCash;
    
    if (detailedLogging) {
        Log::out() << "\n--- Portfolio Value Calculation ---" << std::endl;
        Log::out() << "Cash: $" << std::fixed << std::setprecision(2) << currentCash << std::endl;
    }
    
    // Track total long and short values separately for reporting
    double totalLongValue = 0.0;
    double totalShortValue = 0.0;
    
    for (const auto& pair : positionsByTicker) {
        const Position& position = pair.second;
        std::string ticker = position.getTicker();
        double quantity = position.getQuantity();
        double avgPrice = position.getAvgPrice();
        double currentPrice = getLatestPrice(ticker);
        double positionValue = quantity * currentPrice;
        
        // Add to portfolio value (works for both long and short since quantity will be negative for shorts)
        portfolioValue += positionValue;
        
        // Track long and short values separately
        if (quantity > 0) {
            totalLongValue += positionValue;
        } else {
            totalShortValue += positionValue; // This will be negative for shorts
        }
        
        if (detailedLogging) {
            // Calculate unrealized P&L
            double unrealizedPnL = quantity * (currentPrice - avgPrice);
            double unrealizedPnLPercent = ((currentPrice / avgPrice) - 1.0) * PERCENT;
            if (quantity < 0) {
                // For shorts, profit/loss is reversed
                unrealizedPnL = -unrealizedPnL;
                unrealizedPnLPercent = -unrealizedPnLPercent;
            }
            
            Log::out() << ticker << ": " 
                      << (quantity > 0 ? "LONG " : "SHORT ")
                      << std::abs(quantity) << " shares @ $" << avgPrice
                      << ", current price: $" << currentPrice
                      << ", value: $" << positionValue
                      << ", unrealized P&L: $" << unrealizedPnL
                      << " (" << (unrealizedPnLPercent >= 0 ? "+" : "") 
                      << unrealizedPnLPercent << "%)" << std::endl;
        }
    }
    
    // Update current equity
    currentEquity = portfolioValue;
    
    if (detailedLogging) {
        Log::out() << "Total Long Value: $" << std::fixed << std::setprecision(2) << totalLongValue << std::endl;
        Log::out() << "Total Short Value: $" << std::fixed << std::setprecision(2) << totalShortValue << std::endl;
        Log::out() << "Total Portfolio Value: $" << std::fixed << std::setprecision(2) << portfolioValue << std::endl;
        Log::out() << "--------------------------------\n" << std::endl;
    }
    
    // Update highest equity for drawdown calculation
    highestEquity = std::max(highestEquity, currentEquity);
}

Position
SimulatedBroker::getLatestPosition(std::string ticker)
{
    // Return existing position or empty position
    if (positionsByTicker.find(ticker) != positionsByTicker.end()) {
        return positionsByTicker[ticker];
    } else {
        // Return empty position
        return Position(ticker, 0, 0);
    }
}

void
SimulatedBroker::updateExitLevels(const Order& order)
{
    const std::string& ticker = order.getTicker();
    auto position = positionsByTicker.find(ticker);
    if (position == positionsByTicker.end() || position->second.getQuantity() == 0) {
        exitLevels.erase(ticker);    // flat: nothing left to protect
        return;
    }

    // An order in the same direction as the position (opening it or adding to it) sets its exits
    bool sameDirection = (position->second.getQuantity() > 0) == order.isBuy();
    if (sameDirection && (order.getStopLossPrice() > 0 || order.getTakeProfitPrice() > 0))
        exitLevels[ticker] = ExitLevels{order.getStopLossPrice(), order.getTakeProfitPrice()};
}

void
SimulatedBroker::exitPosition(const std::string& ticker, double quantity, const char* reason, double level)
{
    double currentPrice = getLatestPrice(ticker);
    Log::out() << (quantity > 0 ? "LONG" : "SHORT") << " position " << reason << " triggered for " << ticker
               << " at $" << currentPrice << " (level " << level << ")" << std::endl;

    // Close the whole position: sell a long, buy back a short
    Order exitOrder(quantity > 0 ? OrderType::SELL : OrderType::BUY, ticker, std::fabs(quantity), currentPrice);
    executeOrder(exitOrder);
}

void 
SimulatedBroker::checkStopLosses()
{
    std::map<std::string, Position> positionsCopy = positionsByTicker;   // we may modify positions below
    for (const auto& [ticker, position] : positionsCopy) {
        auto levels = exitLevels.find(ticker);
        double quantity = position.getQuantity();
        if (quantity == 0 || levels == exitLevels.end() || levels->second.stopLoss <= 0
            || (severalStocks && ticker != currentCondition.Ticker))   // several stocks: each on its own bar
            continue;

        double price = getLatestPrice(ticker);
        double stop = levels->second.stopLoss;
        bool hit = quantity > 0 ? price <= stop    // long: price fell to the stop
                                : price >= stop;   // short: price rose to the stop
        if (hit)
            exitPosition(ticker, quantity, STOP_LOSS, stop);
    }
}

void 
SimulatedBroker::checkTakeProfits()
{
    std::map<std::string, Position> positionsCopy = positionsByTicker;   // we may modify positions below
    for (const auto& [ticker, position] : positionsCopy) {
        auto levels = exitLevels.find(ticker);
        double quantity = position.getQuantity();
        if (quantity == 0 || levels == exitLevels.end() || levels->second.takeProfit <= 0
            || (severalStocks && ticker != currentCondition.Ticker))   // several stocks: each on its own bar
            continue;

        double price = getLatestPrice(ticker);
        double target = levels->second.takeProfit;
        bool hit = quantity > 0 ? price >= target    // long: price rose to the target
                                : price <= target;   // short: price fell to the target
        if (hit)
            exitPosition(ticker, quantity, TAKE_PROFIT, target);
    }
}

void 
SimulatedBroker::nextStep()
{
    process();
    
    // Note: The StrategyTester now controls the time stepping and data handling,
    // so this method just runs the current step's processing
    
    // Move to the next market condition
    step++;
}

// Getters for performance metrics
double 
SimulatedBroker::getPnL() const 
{
    return currentEquity - startingCapital;
}

double 
SimulatedBroker::getCurrentEquity() const 
{
    return currentEquity;
}

double 
SimulatedBroker::getStartingCapital() const 
{
    return startingCapital;
}

double 
SimulatedBroker::getDrawdown() const 
{
    if (highestEquity == 0) return 0;
    return (highestEquity - currentEquity) / highestEquity * PERCENT;
}

int 
SimulatedBroker::getNumTrades() const 
{
    return totalTrades;
}

const std::vector<Order>& 
SimulatedBroker::getFilledOrders() const 
{
    return filledOrders;
}

// Setters for simulation parameters
void 
SimulatedBroker::setSlippage(double slippagePerc) 
{
    double oldSlippage = slippagePercentage;
    slippagePercentage = slippagePerc;
    
    detailLog() << "SimulatedBroker: Slippage changed from ±" 
              << std::fixed << std::setprecision(3) << (oldSlippage * PERCENT) 
              << "% to ±" << (slippagePercentage * PERCENT) << "%" << std::endl;
}

void 
SimulatedBroker::setCommission(double commissionPerc) 
{
    commissionPerTrade = commissionPerc;
}

void 
SimulatedBroker::setStartingCapital(double capital) 
{
    startingCapital = capital;
    currentCash = capital;
    currentEquity = capital;
    highestEquity = capital;
}

void
SimulatedBroker::setMarketData(MarketData& MarketData) 
{
    marketData = MarketData;
}