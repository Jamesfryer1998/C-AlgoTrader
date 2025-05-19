#include "SimulatedBroker.hpp"
#include <algorithm>
#include <random>
#include <cmath>

SimulatedBroker::SimulatedBroker(MarketData& marketdata)
: marketData(marketdata)
{
    brokerName = "Simulated";
    
    // Default values
    startingCapital = 100000.0;
    currentCash = startingCapital;
    currentEquity = startingCapital;
    highestEquity = startingCapital;
    slippagePercentage = 0.0005; // 0.05% default slippage
    commissionPerTrade = 1.0;    // $1 per trade default commission
    totalTrades = 0;
    step = 0;
    detailedLogging = false; // Detailed logging disabled by default
    
    // Random seed initialization
    useFixedSeed = false;
    randomSeed = 42; // Default seed
    
    // Log the default settings
    std::cout << "SimulatedBroker initialized with:"
              << "\n  - Starting capital: $" << startingCapital
              << "\n  - Commission: $" << commissionPerTrade << " per trade"
              << "\n  - Slippage: ±" << (slippagePercentage * 100.0) << "% (random variation)"
              << std::endl;
    
    connect();
}

void SimulatedBroker::enableFixedRandomSeed(unsigned int seed)
{
    useFixedSeed = true;
    randomSeed = seed;
    std::cout << "Using fixed random seed: " << seed << " for deterministic testing" << std::endl;
}

void SimulatedBroker::enableDetailedLogging(bool enable)
{
    detailedLogging = enable;
    std::cout << "SimulatedBroker: Detailed logging " << (enable ? "enabled" : "disabled") << std::endl;
}

SimulatedBroker::~SimulatedBroker()
{
    disconnect();
}

void
SimulatedBroker::process()
{
    // Check for valid market data
    auto data = marketData.getData();
    if(data.size() == 0)
    {
        throw std::runtime_error("No market data found");
    }

    // Always refresh the current condition to ensure we're using the latest data
    // This is critical for timestamp consistency between strategy signals and order execution
    currentCondition = marketData.getData()[step];
    simulationTime = currentCondition.DateTime;
    
    // Log the current time step being processed
    std::cout << "SimulatedBroker processing time step: " << simulationTime << std::endl;
    
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
    std::cout << "Connected to simulated broker with $" << startingCapital << " capital" << std::endl;
    return 1;
}

int
SimulatedBroker::disconnect()
{
    std::cout << "Disconnected from simulated broker" << std::endl;
    std::cout << "Final equity: $" << currentEquity << std::endl;
    std::cout << "Total P&L: $" << (currentEquity - startingCapital) << std::endl;
    std::cout << "Total trades: " << totalTrades << std::endl;
    return 1;
}

double 
SimulatedBroker::getLatestPrice(std::string ticker)
{
    // Check if ticker matches our current condition
    if (ticker == currentCondition.Ticker) {
        return currentCondition.Close;
    }
    
    // If we had multiple tickers, we'd need more complex logic here
    std::cerr << "Warning: Requested price for ticker " << ticker 
              << " but current data is for " << currentCondition.Ticker << std::endl;
    return currentCondition.Close;
}

int 
SimulatedBroker::placeOrder(oms::Order order)
{
    // Add order to pending orders queue
    pendingOrders.push_back(order);
    
    // Update to ensure we're using the most current timestamp from MarketData
    // Get fresh current data from MarketData to ensure timestamp consistency
    currentCondition = marketData.getCurrentData();
    simulationTime = currentCondition.DateTime;
    
    std::cout << "oms::Order placed for order " 
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
    std::vector<oms::Order> remainingOrders;
    
    for (auto& order : pendingOrders) {
        if (checkOrderValidity(order)) {
            executeOrder(order);
        } else {
            // Keep invalid orders for the next cycle
            remainingOrders.push_back(order);
        }
    }
    
    // Update pending orders with those that weren't processed
    pendingOrders = remainingOrders;
}

bool
SimulatedBroker::checkOrderValidity(const oms::Order& order) const
{
    // For a real system, we'd perform more validation:
    // - Check if we have enough cash for buy orders
    // - Check if we have enough shares for sell orders
    // - Check if order parameters are valid
    // - Check market hours, etc.
    
    return true;
}

void
SimulatedBroker::executeOrder(oms::Order& order)
{
    // Get current price for the ticker
    float basePrice = getLatestPrice(order.getTicker());
    float originalOrderPrice = order.getPrice();
    
    double slippageMultiplier = 1.0;
    double actualSlippagePercent = 0.0;
    
    if (slippagePercentage > 0.0) {
        std::mt19937 gen;
        
        if (useFixedSeed) {
            // Use fixed seed for tests
            gen.seed(randomSeed);
        } else {
            // Use random seed for real trading
            std::random_device rd;
            gen.seed(rd());
        }
        
        std::uniform_real_distribution<> slippageDist(-slippagePercentage, slippagePercentage);
        actualSlippagePercent = slippageDist(gen);
        slippageMultiplier = 1.0 + actualSlippagePercent;
    }
    
    double executionPrice = basePrice * slippageMultiplier;
    
    // For limit orders, check price constraints
    if (order.getType() == OrderType::LIMIT_BUY && executionPrice > order.getPrice()) {
        // Cannot execute buy limit order above limit price
        std::cout << "Limit Buy order not executed: Market price $" << executionPrice 
                  << " above limit price $" << order.getPrice() << std::endl;
        return;
    } else if (order.getType() == OrderType::LIMIT_SELL && executionPrice < order.getPrice()) {
        // Cannot execute sell limit order below limit price
        std::cout << "Limit Sell order not executed: Market price $" << executionPrice 
                  << " below limit price $" << order.getPrice() << std::endl;
        return;
    }
    
    // Apply commission cost
    currentCash -= commissionPerTrade;
    
    // Update positions based on order
    updatePositions(order, executionPrice);
    
    // Add to filled orders
    order.setPrice(executionPrice); // Update with actual execution price
    filledOrders.push_back(order);
    totalTrades++;
    
    std::cout << "oms::Order executed: " << (order.isBuy() ? "BUY" : "SELL")
              << " " << order.getQuantity() << " shares of " << order.getTicker()
              << " at $" << std::fixed << std::setprecision(2) << executionPrice;
    
    // Display slippage information
    if (slippagePercentage > 0.0) {
        std::cout << " (oms::Order price: $" << std::fixed << std::setprecision(2) << originalOrderPrice
                  << ", Slippage: " << (actualSlippagePercent >= 0 ? "+" : "")
                  << std::fixed << std::setprecision(3) << (actualSlippagePercent * 100.0) << "%)";
    }
    
    std::cout << std::endl;
}

void
SimulatedBroker::updatePositions(const oms::Order& order, double executionPrice)
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
            oms::Position& existingPos = positionsByTicker[ticker];
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
                        std::cout << "Covered short position for " << ticker << " and established long position of " 
                                  << newTotalShares << " shares" << std::endl;
                    } else {
                        // Exactly covered the short position, close position
                        positionHistory.push_back(existingPos);
                        positionsByTicker.erase(ticker);
                        std::cout << "Completely covered short position for " << ticker << std::endl;
                    }
                } else {
                    // Partially covering short position
                    existingPos.setQuantity(newTotalShares);
                    // We don't update avg price for partial short covers
                    std::cout << "Partially covered short position for " << ticker 
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
                std::cout << "Increased long position for " << ticker << " to " 
                          << newTotalShares << " shares at avg price $" 
                          << std::fixed << std::setprecision(2) << newAvgPrice << std::endl;
            }
        } else {
            // New position
            oms::Position newPosition(ticker, quantity, executionPrice);
            positionsByTicker[ticker] = newPosition;
            std::cout << "Established new long position for " << ticker << " with " 
                      << quantity << " shares at $" << executionPrice << std::endl;
        }
    } else {
        // Selling
        currentCash += orderTotal;
        
        if (positionsByTicker.find(ticker) != positionsByTicker.end()) {
            // We have an existing position for this ticker
            oms::Position& existingPos = positionsByTicker[ticker];
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
                        std::cout << "Closed long position for " << ticker 
                                  << " and established short position of " 
                                  << -newTotalShares << " shares" << std::endl;
                    } else {
                        // Exactly closed the long position
                        positionHistory.push_back(existingPos);
                        positionsByTicker.erase(ticker);
                        std::cout << "Completely closed long position for " << ticker << std::endl;
                    }
                } else {
                    // Partially reducing long position
                    existingPos.setQuantity(newTotalShares);
                    // Average price remains unchanged when reducing a long position
                    std::cout << "Partially closed long position for " << ticker 
                              << ", remaining: " << newTotalShares << " shares" << std::endl;
                }
            } else {
                // Currently short, selling to increase short position
                double newTotalShares = existingQuantity - quantity;
                // The average price is weighted for short positions too
                double newAvgPrice = (existingPos.getAvgPrice() + executionPrice) / 2;
                
                existingPos.setQuantity(newTotalShares);
                existingPos.setAvgPrice(newAvgPrice);
                std::cout << "Increased short position for " << ticker << " to " 
                          << -newTotalShares << " shares at avg price $" 
                          << std::fixed << std::setprecision(2) << newAvgPrice << std::endl;
            }
        } else {
            // No existing position, establishing a new short position
            oms::Position newPosition(ticker, -quantity, executionPrice);
            positionsByTicker[ticker] = newPosition;
            std::cout << "Established new short position for " << ticker << " with " 
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
        std::cout << "\n--- Portfolio Value Calculation ---" << std::endl;
        std::cout << "Cash: $" << std::fixed << std::setprecision(2) << currentCash << std::endl;
    }
    
    // Track total long and short values separately for reporting
    double totalLongValue = 0.0;
    double totalShortValue = 0.0;
    
    for (const auto& pair : positionsByTicker) {
        const oms::Position& position = pair.second;
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
            double unrealizedPnLPercent = ((currentPrice / avgPrice) - 1.0) * 100.0;
            if (quantity < 0) {
                // For shorts, profit/loss is reversed
                unrealizedPnL = -unrealizedPnL;
                unrealizedPnLPercent = -unrealizedPnLPercent;
            }
            
            std::cout << ticker << ": " 
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
        std::cout << "Total Long Value: $" << std::fixed << std::setprecision(2) << totalLongValue << std::endl;
        std::cout << "Total Short Value: $" << std::fixed << std::setprecision(2) << totalShortValue << std::endl;
        std::cout << "Total Portfolio Value: $" << std::fixed << std::setprecision(2) << portfolioValue << std::endl;
        std::cout << "--------------------------------\n" << std::endl;
    }
    
    // Update highest equity for drawdown calculation
    highestEquity = std::max(highestEquity, currentEquity);
}

oms::Position
SimulatedBroker::getLatestPosition(std::string ticker)
{
    // Return existing position or empty position
    if (positionsByTicker.find(ticker) != positionsByTicker.end()) {
        return positionsByTicker[ticker];
    } else {
        // Return empty position
        return oms::Position(ticker, 0, 0);
    }
}

void 
SimulatedBroker::checkStopLosses()
{
    // Create a copy of positionsByTicker to iterate, as we might modify during iteration
    std::map<std::string, oms::Position> positionsCopy = positionsByTicker;
    
    // Iterate through positions and check if current price hits stop loss
    for (const auto& pair : positionsCopy) {
        const oms::Position& position = pair.second;
        std::string ticker = position.getTicker();
        double quantity = position.getQuantity();
        
        // Skip positions with zero quantity
        if (quantity == 0) {
            continue;
        }
        
        // Find associated order with stop loss
        for (const auto& order : filledOrders) {
            if (order.getTicker() == ticker && order.getStopLossPrice() > 0) {
                double currentPrice = getLatestPrice(ticker);
                
                // Handle stop loss differently for long and short positions
                if (quantity > 0) {
                    // For LONG positions: Stop loss triggers when price falls below stop level
                    if (currentPrice <= order.getStopLossPrice()) {
                        std::cout << "LONG position stop loss triggered for " << ticker 
                                  << " at $" << currentPrice 
                                  << ", stop price: " << order.getStopLossPrice() << std::endl;
                        
                        // For long positions, we SELL to exit
                        oms::Order stopOrder(OrderType::SELL, ticker, std::abs(order.getQuantity()), currentPrice);
                        executeOrder(stopOrder);
                        break;
                    }
                } else {
                    // For SHORT positions: Stop loss triggers when price rises above stop level
                    if (currentPrice >= order.getStopLossPrice()) {
                        std::cout << "SHORT position stop loss triggered for " << ticker 
                                  << " at $" << currentPrice 
                                  << ", stop price: " << order.getStopLossPrice() << std::endl;
                        
                        // For short positions, we BUY to cover and exit
                        oms::Order stopOrder(OrderType::BUY, ticker, std::abs(order.getQuantity()), currentPrice);
                        executeOrder(stopOrder);
                        break;
                    }
                }
            }
        }
    }
}

void 
SimulatedBroker::checkTakeProfits()
{
    // Create a copy of positionsByTicker to iterate, as we might modify during iteration
    std::map<std::string, oms::Position> positionsCopy = positionsByTicker;
    
    // Iterate through positions and check if current price hits take profit
    for (const auto& pair : positionsCopy) {
        const oms::Position& position = pair.second;
        std::string ticker = position.getTicker();
        double quantity = position.getQuantity();
        
        // Skip positions with zero quantity
        if (quantity == 0) {
            continue;
        }
        
        // Find associated order with take profit
        for (const auto& order : filledOrders) {
            if (order.getTicker() == ticker && order.getTakeProfitPrice() > 0) {
                double currentPrice = getLatestPrice(ticker);
                
                // Handle take profit differently for long and short positions
                if (quantity > 0) {
                    // For LONG positions: Take profit triggers when price rises above target level
                    if (currentPrice >= order.getTakeProfitPrice()) {
                        std::cout << "LONG position take profit triggered for " << ticker 
                                  << " at $" << currentPrice 
                                  << ", take profit price: " << order.getTakeProfitPrice() << std::endl;
                        
                        // For long positions, we SELL to exit with profit
                        oms::Order tpOrder(OrderType::SELL, ticker, std::abs(quantity), currentPrice);
                        executeOrder(tpOrder);
                        break;
                    }
                } else {
                    // For SHORT positions: Take profit triggers when price falls below target level
                    if (currentPrice <= order.getTakeProfitPrice()) {
                        std::cout << "SHORT position take profit triggered for " << ticker 
                                  << " at $" << currentPrice 
                                  << ", take profit price: " << order.getTakeProfitPrice() << std::endl;
                        
                        // For short positions, we BUY to cover and exit with profit
                        oms::Order tpOrder(OrderType::BUY, ticker, std::abs(quantity), currentPrice);
                        executeOrder(tpOrder);
                        break;
                    }
                }
            }
        }
    }
}

void 
SimulatedBroker::nextStep()
{
    process();
    
    // Note: The Backtester now controls the time stepping and data handling,
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
    return (highestEquity - currentEquity) / highestEquity * 100.0;
}

int 
SimulatedBroker::getNumTrades() const 
{
    return totalTrades;
}

const std::vector<oms::Order>& 
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
    
    std::cout << "SimulatedBroker: Slippage changed from ±" 
              << std::fixed << std::setprecision(3) << (oldSlippage * 100.0) 
              << "% to ±" << (slippagePercentage * 100.0) << "%" << std::endl;
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