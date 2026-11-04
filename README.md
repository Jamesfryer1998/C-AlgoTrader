# C++ Algorithmic Trading System

A modern C++ framework for algorithmic trading, backtesting strategies, and live trading.

## **Building the Project**

### Prerequisites
- CMake 3.3 or higher
- C++23 compatible compiler
- nlohmann_json library (automatically fetched)
- GoogleTest (automatically fetched)

### To Build
```sh
# Create a build directory
mkdir -p build
cd build

# Configure the project
cmake ..

# Build
make -j4
```

### To Run (live, Alpaca)
The trading app now streams real-time 1-minute bars from Alpaca and sends orders to Alpaca's API.
No CSV files or broker desktop app are needed.

1. Create a free account at [alpaca.markets](https://alpaca.markets) and generate **paper trading** API keys.
2. Put the keys in a `.env` file in the project root (git ignores it, so they never get committed):
   ```sh
   cp .env.example .env
   # then edit .env and paste in your key id and secret
   ```
   A variable already exported in your shell takes priority over `.env`.
3. Build and run from the project root:
   ```sh
   ./build_run.sh
   ```
   Stop it with `Ctrl+C`.

Settings live in the `"alpaca"` block of `src/config/config.json`:

| Key | Meaning |
|---|---|
| `data_feed` | `"iex"` free real-time feed, `"sip"` all exchanges (paid plan), `"test"` fake symbol FAKEPACA that streams 24/7 for checking the wiring (orders are only printed) |
| `trading_url` | `https://paper-api.alpaca.markets` (paper) or `https://api.alpaca.markets` (real money) |
| `dry_run` | `true` prints orders instead of sending them |

At start-up the app back-fills exactly the bars the active strategies need (the largest of: RSI `period`, MEANREV `lookback_period`/`moving_average_period`, MACD `5 × (long_period + signal_period)`) and keeps that many in memory, so every strategy decides from the first live bar.

Outside US market hours, set `"data_feed": "test"` to see bars flowing.

#### Saved state and P&L
While running, the app saves its state to `data/live/` (`data/live_dry_run/` for dry runs). The files are only written while running and only read once at start-up, so they never slow down a decision:

| File | Contents |
|---|---|
| `trades.csv` | Every order the strategies produced, one line each, whatever happened to it (see below) |
| `positions.csv` | Current positions, rewritten when they change |
| `performance.json` | Latest snapshot: `alpaca` = Alpaca's real equity, today's P&L and position (unrealised P&L from real fills and Alpaca's latest price); `estimate` = our own book (realised / unrealised P&L, ROI, drawdown, Sharpe, win rate) |

`trades.csv` columns:

| Column | Meaning |
|---|---|
| `our_id` | Our order number. Every attempt gets one, and numbering carries on after a restart |
| `time` | Bar the decision was made on (New York time) |
| `sent_at_utc` | Wall-clock time the order was sent, to the millisecond |
| `strategy` | Strategy that produced it (`RSI+MACD` if several agreed, `broker` for adjustments) |
| `ticker`, `side`, `type`, `order_class` | e.g. `NVDA`, `buy`, `BUY`, `bracket` |
| `quantity`, `price` | Price is the bar close when decided (an estimate of the fill) |
| `stop_loss`, `take_profit` | Bracket prices sent with the order |
| `status` | `sent`, `rejected` (by Alpaca), `blocked` (by our risk checks), `adjustment`, `dry_run` |
| `broker_status` | Alpaca's own status, e.g. `accepted` |
| `client_order_id` | Our id as sent to Alpaca, e.g. `algo-12-1793300000`: search for it on Alpaca's dashboard |
| `broker_order_id` | Alpaca's id for the order |
| `round_trip_ms` | Time to send the order and get Alpaca's reply |
| `note` | Why it was blocked/rejected, or why an adjustment was made |

Lines are written by a background thread after the order has gone out, so saving never delays an order.

On the next start the trades are replayed, so P&L and order numbering carry on. Each bar the app also checks
its position with Alpaca; if Alpaca filled a stop loss or take profit by itself, the difference is recorded as a
trade at that bar's close. After every bar the app prints one `P&L:` line straight from Alpaca: equity, today's P&L, the position
and its unrealised P&L. Our own book (which assumes fills at bar closes and feeds drawdown / Sharpe /
win rate) is saved in `performance.json` under `estimate`; it's only printed in dry runs, where Alpaca
isn't holding the trades. ROI is measured against `"starting_capital"` in the config.
See [doc/realtime_data_and_brokers.md](doc/realtime_data_and_brokers.md) for why Alpaca, prices and speeds.

On macOS the WebSocket library uses Apple's built-in TLS, so there is nothing extra to install.
On Linux install OpenSSL headers first (`sudo apt install libssl-dev`).

### To Run Backtester
```sh
# Run from the project root directory
./build_run_backtest.sh --start-date 2026-10-01 --end-date 2026-10-28 --download
```
`--download` fetches any days in the range that aren't already in `data/` from Alpaca first
(uses the keys in `.env`). Use `--download-only` to just fetch data. One CSV per trading day is saved as
`data/marketData_<TICKER>_<YYYY-MM-DD>.csv`, in New York exchange time, regular hours only.

By default history comes from Alpaca's `sip` feed (all US exchanges, free for data older than 15 minutes).
Set `"history_feed": "iex"` in the `"alpaca"` config block to match the free live stream instead.

### Optimising strategy settings
`optimise_app` backtests every combination of strategy settings listed in `src/config/optimisation.json`
over years of 1-minute data, in parallel, and ranks them.

```sh
# 1. Download the history once (about 10 years of NVDA, ~60 MB; re-running only fetches what's new)
./build/app/optimise_app --download --strategy RSI --max-combinations 1

# 2. Optimise all three strategies (RSI, MACD, MEANREV)
./build/app/optimise_app

# Or just one, or a quicker random sample of settings
./build/app/optimise_app --strategy MACD --max-combinations 200
```

How it avoids fooling itself:
- The last `test_months` (default 12) are **held back**. Every setting is ranked on the older data only;
  the top 10 are then run on the held-back year as a check. Settings are never chosen using the test year.
- If the best training setting has a negative Sharpe on the test year, it says so: that usually means it
  fitted noise in the past rather than finding a real edge.
- Costs match Alpaca: $0 commission plus 0.01% slippage against you on every fill (`optimisation.json`).
- Settings need at least `min_closed_trades` (default 30) on the training data to be ranked.

Output (in `data/optimisation/`):
- `<STRATEGY>_<time>.csv` - every setting tried with training and test results, plus your current
  `config.json` settings as `current_config` for comparison
- `best_<time>.json` - the best training setting per strategy, ready to copy into `config.json`

Edit the value lists in `src/config/optimisation.json` to search different ranges (no rebuild needed).
`"objective"` can be `sharpe` (default), `total_return` or `return_over_drawdown`.

Returns look tiny (fractions of a percent) because every order is 1 share against $100k of capital;
compare settings by Sharpe ratio, which doesn't depend on trade size.

### Quant learning projects (`quant_app`)
Three classic projects on **daily** data, separate from the live NVDA trader. Prices come from Alpaca
(same `.env` keys, adjusted for splits and dividends, history from 2016) and are cached in `data/daily/`.
Results are printed and saved as CSVs in `data/quant/`. Code is in `src/quant/`.

```sh
# Run from the project root directory
./build/app/quant_app risk                      # Monte Carlo VaR / expected shortfall
./build/app/quant_app scan                      # which pairs of stocks are cointegrated?
./build/app/quant_app pairs --pair KO,PEP       # pairs trading backtest
./build/app/quant_app macross --symbol SPY      # 50/200-day crossover vs buy and hold
./build/app/quant_app --help                    # all options (symbols, weights, z-score levels, costs...)
```
Add `--offline` to reuse the saved CSVs instead of downloading again.

**1. Monte Carlo portfolio risk** (`risk`). Estimates each stock's return, volatility and the correlation
matrix from daily log returns, then simulates 10,000 possible 21-day futures with correlated random returns
(Cholesky factor of the covariance). From that distribution: **VaR 95%** (the loss a 1-in-20 month exceeds)
and **expected shortfall** (the average loss in those worst 5%). The same simulation with every correlation set
to zero shows that correlation, not the individual stocks, drives portfolio risk. A reality check counts how
often real 21-day windows lost more than the VaR: usually more than the model's 5%, because real returns have
fatter tails than a normal distribution.

**2. Pairs trading** (`scan`, `pairs`). `scan` runs an Engle-Granger cointegration test on every pair in a list
(regress log Y on log X, then an ADF test on the residual spread) and ranks them; it warns that testing many pairs
finds a few by chance. `pairs` backtests one pair: the hedge ratio and z-score come from a rolling 252-day fit,
enter when |z| > 2 (short the expensive stock, buy the cheap one), exit when |z| < 0.5, stop out at |z| > 4.
The hedge ratio is frozen while a trade is open, signals earn returns from the next day, and each trade pays
costs. A year-by-year cointegration table shows that relationships break.

**3. Moving average crossover** (`macross`). Invested while the 50-day average is above the 200-day, in cash
otherwise. The signal from one close only earns returns from the next day, every buy and sell pays costs, and
the result is compared with buy and hold over exactly the same days. It also prints the same strategy without
costs and with look-ahead (using the same close it trades on) to show how much those mistakes flatter a backtest.

### Strategy screen (`screen_app`)
Which stock / strategy combinations look worth trading? `screen_app` backtests RSI, MACD and MEANREV on every
stock in `src/config/screen.json` (the S&P 100 by default) using daily bars since 2016:

1. **Training years** (before `test_start`, 2024-01-01 by default): every setting in each strategy's grid is
   backtested and the one with the best Sharpe is picked (it needs at least `min_closed_trades` trades).
2. **Test years** (from `test_start`): only that pick is run, on data it was never tuned on, and compared with
   buying and holding the same stock over the same days.

```sh
# Run from the project root directory (downloads daily prices into data/daily/ the first time)
./build/app/screen_app                          # all 100 stocks, all three strategies (a few minutes)
./build/app/screen_app --symbols KO,PEP,MSFT    # a few stocks
./build/app/screen_app --strategies MEANREV     # one strategy
./build/app/screen_app --resume                 # carry on after stopping it part way
./build/app/screen_app --report                 # reprint the report from the saved results
```

The report shows, per strategy, how often the training pick made money and beat buy and hold on the test
years, and whether a better training Sharpe predicted a better test Sharpe. It then lists the 10 best
combinations by training Sharpe (what you'd pick knowing only the past) with how they did afterwards, and the
combinations that held up in both periods. Every row is saved in `data/screen/screen_results.csv`.

Read it with care: hundreds of combinations are tried, so some will look good by luck; today's S&P 100 is a
list of past winners (survivorship bias); and "held up" is filtered using the test years. Treat what it finds
as candidates to paper trade, not proof.

### Running Tests
```sh
# From the build directory
ctest
# or
make test
```
Individual test executables are located in subdirectories:
```
./build/tests/data_access_tests/data_access_tests
./build/tests/oms_tests/oms_tests
./build/tests/strategy_tests/strategy_tests
./build/tests/broker_tests/broker_tests
./build/tests/backtest_tests/backtest_tests
./build/tests/util_tests/util_tests
./build/tests/alpaca_tests/alpaca_tests
./build/tests/regression_tests/regression_tests
```

`regression_tests` runs the whole backtest and the live decision path on a fixed price series and checks the
results haven't changed. If you change behaviour on purpose (e.g. tune a strategy), it prints the new values
on a `REGRESSION VALUES` line; copy them into `tests/regression_tests/Tests_Regression.cpp`.

### To Debug Crash
```sh
lldb ./build/app/algo_trader_app
run
bt
```

## **Architecture**  

### **System Overview**  
<details>
  <summary>View Diagram</summary>

  ![](./doc/system_overview.drawio.png)  

</details>  

The **System Overview** provides a high-level architecture of the trading platform. It includes:  
- **Live Data** (Alpaca WebSocket pushes a 1-minute bar the moment each minute closes, `src/alpaca/AlpacaStream`)  
- **Historical Data** (downloaded from Alpaca into CSV files in `data/`, used by the backtester)  
- **C++ Backend** (Processes data, runs strategies, and manages orders)  
- **Broker API Integration** (Alpaca REST API executes trades, `src/alpaca/AlpacaBroker`)  

Live flow, all in one process:
```
Alpaca WebSocket --(bar)--> MarketData --> Strategies (RSI/MACD/MEANREV) --(signal)--> OMS --> Alpaca REST order
```

---

### **Data Reader**  
<details>
  <summary>View Diagram</summary>

  ![](./doc/data_reader.drawio.png)  

</details>  

The **Data Reader** is responsible for:  
- Reading market data from CSV files  
- Parsing and structuring the data for the trading system  
- Feeding the data into the **Strategy Engine** for signal generation  

---

### **Data Flow**  
<details>
  <summary>View Diagram</summary>

  ![](./doc/data_flow.drawio.png)  

</details>  

The **Data Flow** diagram illustrates how data moves through the system when **backtesting**:  
1. **`backtest_app --download` fetches 1-minute bars from Alpaca** and saves one CSV per day in `data/`  
2. **C++ reads the CSV** and processes market conditions  
3. **Strategies analyze the data** and generate trading signals  
4. **Orders are sent to the OMS**, which manages execution via the simulated broker  

When trading **live**, steps 1 and 2 are replaced by the Alpaca WebSocket stream and orders go to Alpaca (see System Overview).  

---

### **Order Management System (OMS)**  
<details>
  <summary>View Diagram</summary>

  ![](./doc/oms.drawio.png)  

</details>  

The **Order Management System (OMS)** handles:  
- Storing open **orders** (buy/sell requests)  
- Tracking **positions** (executed orders)  
- Communicating with the broker API for order execution  
- Managing order status updates (e.g., filled, canceled)  

---

### **Strategy Engine**  
<details>
  <summary>View Diagram</summary>

  ![](./doc/strategy_engine.drawio.png)  

</details>  

The **Strategy Engine** is the core of trade decision-making:  
- Receives **market data** and analyzes trading conditions  
- Runs **trading strategies** (e.g., Moving Average Crossover, RSI)  
- Generates **trade signals** (buy/sell orders)  
- Sends orders to the OMS for execution  

---

### **Backtest Engine**  
<details>
  <summary>View Diagram</summary>

  ![](./doc/backtest.drawio.png)  

</details>  

The **Backtest Engine** allows testing strategies on historical data:  
- **Simulates market conditions** by feeding past data to strategies  
- **Executes trades** as if they were placed in real-time  
- **Evaluates strategy performance** using metrics like win/loss ratio, Sharpe ratio, and drawdown  
- Helps refine trading algorithms before live deployment