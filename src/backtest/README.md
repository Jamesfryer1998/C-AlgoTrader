# Backtesting System

This module provides backtesting functionality for the algo trading system.

## Design

The backtesting system is designed around the Adapter pattern to isolate the backtesting logic from the core trading components. This approach ensures that the core components (MarketData, StrategyEngine, OMS, etc.) don't need special "backtest mode" flags or conditional logic.

## Important Usage Notes

### Historical Data for Strategies

When using this backtesting system, it's important to understand that strategies (like RSI) often require historical data to make calculations. The adapter ensures that:

1. Each strategy only sees data up to the current simulation time point
2. As the simulation progresses, more historical data becomes available
3. Strategies won't see "future" data points

For example, an RSI strategy with a period of 14 will need at least 14 data points before it can make a proper calculation. The backtester will automatically accumulate data as time advances.

### Key Components

1. **BacktestMarketDataAdapter**: 
   - Wraps a MarketData instance
   - Manages the time sequence for backtesting
   - Updates the MarketData instance with accumulated data points as time advances
   - Handles data loading and indexing without affecting the core MarketData implementation

2. **Backtester**:
   - Coordinates the overall backtesting process
   - Manages the BacktestMarketDataAdapter
   - Controls simulation flow (time steps, execution)
   - Calculates performance metrics

3. **SimulatedBroker**:
   - Implementation of BrokerBase for simulating order execution
   - Handles simulated slippage, commission, position management
   - Provides performance tracking (equity, P&L, drawdown)

## Benefits of this Approach

1. **Isolation**: Backtesting logic is completely separated from the core components.
2. **Maintainability**: Changes to the core system don't affect the backtesting system, and vice versa.
3. **Testability**: Each component can be independently tested.
4. **Flexibility**: Different backtesting approaches can be implemented without modifying the core system.

## Usage

```cpp
// Create a backtester with a configuration
Backtester backtester(algoConfig);

// Configure the backtester
backtester.setStartingCapital(100000.0);
backtester.setCommissionPerTrade(1.0);
backtester.setSlippagePercentage(0.001);
backtester.setDateRange("2023-01-01", "2023-12-31");

// Run the backtest
backtester.run();

// Get the results
const PerformanceMetrics& metrics = backtester.getPerformanceMetrics();
```

## How it Works

1. The Backtester loads historical market data and initializes the adapter.
2. The adapter feeds one data point at a time to MarketData as the simulation runs.
3. For each time step:
   - Adapter advances to the next data point and updates MarketData
   - StrategyEngine runs with the current data
   - Strategies generate orders
   - SimulatedBroker executes orders based on the current data
   - Performance is tracked
4. After completion, performance metrics are calculated and reported.

This approach allows the core trading system components to operate normally without being aware of the simulation environment.