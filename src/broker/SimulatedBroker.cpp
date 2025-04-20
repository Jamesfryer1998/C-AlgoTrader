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
    currentCondition = marketData.getCurrentData();
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

float 
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
SimulatedBroker::placeOrder(Order order)
{
    // Add order to pending orders queue
    pendingOrders.push_back(order);
    
    // Update to ensure we're using the most current timestamp from MarketData
    // Get fresh current data from MarketData to ensure timestamp consistency
    currentCondition = marketData.getCurrentData();
    simulationTime = currentCondition.DateTime;
    
    std::cout << "Order placed for order " 
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
SimulatedBroker::checkOrderValidity(const Order& order) const
{
    // For a real system, we'd perform more validation:
    // - Check if we have enough cash for buy orders
    // - Check if we have enough shares for sell orders
    // - Check if order parameters are valid
    // - Check market hours, etc.
    
    return true;
}

void
SimulatedBroker::executeOrder(Order& order)
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
    
    // Calculate the price difference due to slippage
    // double priceDifference = executionPrice - originalOrderPrice;
    // double slippageImpact = (priceDifference / originalOrderPrice) * 100.0;
    
    std::cout << "Order executed: " << (order.isBuy() ? "BUY" : "SELL")
              << " " << order.getQuantity() << " shares of " << order.getTicker()
              << " at $" << std::fixed << std::setprecision(2) << executionPrice;
    
    // Display slippage information
    if (slippagePercentage > 0.0) {
        std::cout << " (Order price: $" << std::fixed << std::setprecision(2) << originalOrderPrice
                  << ", Slippage: " << (actualSlippagePercent >= 0 ? "+" : "")
                  << std::fixed << std::setprecision(3) << (actualSlippagePercent * 100.0) << "%)";
    }
    
    std::cout << std::endl;
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
            double newTotalShares = existingPos.getQuantity() + quantity;
            double newAvgPrice = ((existingPos.getQuantity() * existingPos.getAvgPrice()) + 
                                 (quantity * executionPrice)) / newTotalShares;
            
            // Update position
            existingPos.setQuantity(newTotalShares);
            existingPos.setAvgPrice(newAvgPrice);
        } else {
            // New position
            Position newPosition(ticker, quantity, executionPrice);
            positionsByTicker[ticker] = newPosition;
        }
    } else {
        // Selling
        currentCash += orderTotal;
        
        if (positionsByTicker.find(ticker) != positionsByTicker.end()) {
            Position& existingPos = positionsByTicker[ticker];
            double newTotalShares = existingPos.getQuantity() - quantity;
            
            // If position would be zero or negative, close it
            if (newTotalShares <= 0) {
                // Save to history before deleting
                positionHistory.push_back(existingPos);
                
                // Remove position
                positionsByTicker.erase(ticker);
            } else {
                // Update position
                existingPos.setQuantity(newTotalShares);
                // Note: We don't update avg price when selling
            }
        } else {
            std::cerr << "Warning: Attempting to sell position not in portfolio: " << ticker << std::endl;
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
    
    for (const auto& pair : positionsByTicker) {
        const Position& position = pair.second;
        double currentPrice = getLatestPrice(position.getTicker());
        portfolioValue += position.getQuantity() * currentPrice;
    }
    
    // Update current equity
    currentEquity = portfolioValue;
    
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
SimulatedBroker::checkStopLosses()
{
    // Create a copy of positionsByTicker to iterate, as we might modify during iteration
    std::map<std::string, Position> positionsCopy = positionsByTicker;
    
    // Iterate through positions and check if current price hits stop loss
    for (const auto& pair : positionsCopy) {
        const Position& position = pair.second;
        std::string ticker = position.getTicker();
        
        // Skip positions with zero quantity
        if (position.getQuantity() <= 0) {
            continue;
        }
        
        // Find associated order with stop loss
        for (const auto& order : filledOrders) {
            if (order.getTicker() == ticker && order.getStopLossPrice() > 0) {
                double currentPrice = getLatestPrice(ticker);
                
                // If price drops below stop loss, create and execute sell order immediately
                if (currentPrice <= order.getStopLossPrice()) {
                    std::cout << "Stop loss triggered for " << ticker 
                              << " at $" << currentPrice 
                              << ", stop price: " << order.getStopLossPrice() << std::endl;
                    
                    // Create and execute stop loss order directly
                    Order stopOrder(OrderType::SELL, ticker, position.getQuantity(), currentPrice);
                    executeOrder(stopOrder);
                    
                    // Break after executing the first stop loss order for this position
                    break;
                }
            }
        }
    }
}

void 
SimulatedBroker::checkTakeProfits()
{
    // Create a copy of positionsByTicker to iterate, as we might modify during iteration
    std::map<std::string, Position> positionsCopy = positionsByTicker;
    
    // Iterate through positions and check if current price hits take profit
    for (const auto& pair : positionsCopy) {
        const Position& position = pair.second;
        std::string ticker = position.getTicker();
        
        // Skip positions with zero quantity
        if (position.getQuantity() <= 0) {
            continue;
        }
        
        // Find associated order with take profit
        for (const auto& order : filledOrders) {
            if (order.getTicker() == ticker && order.getTakeProfitPrice() > 0) {
                double currentPrice = getLatestPrice(ticker);
                
                // If price rises above take profit, create and execute sell order immediately
                if (currentPrice >= order.getTakeProfitPrice()) {
                    std::cout << "Take profit triggered for " << ticker 
                              << " at $" << currentPrice 
                              << ", take profit price: " << order.getTakeProfitPrice() << std::endl;
                    
                    // Create and execute take profit order directly
                    Order tpOrder(OrderType::SELL, ticker, position.getQuantity(), currentPrice);
                    executeOrder(tpOrder);
                    
                    // Break after executing the first take profit order for this position
                    break;
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