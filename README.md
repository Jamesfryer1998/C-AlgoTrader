Here’s your **updated README** with additional descriptions for each section and collapsible image sections:  

---

## **To Run**  
```sh
./build_run.sh
```  
This script builds and runs the trading system. Ensure all dependencies are installed before execution.

---

## **Architecture**  

### **System Overview**  
<details>
  <summary>View Diagram</summary>

  ![](./doc/system_overview.drawio.png)  

</details>  

The **System Overview** provides a high-level architecture of the trading platform. It includes:  
- **Data Fetching** (Python script fetching market data)  
- **Data Storage** (CSV files for historical data)  
- **C++ Backend** (Processes data, runs strategies, and manages orders)  
- **Broker API Integration** (Executes trades)  

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

The **Data Flow** diagram illustrates how data moves through the system:  
1. **Python fetches market data** and saves it to a CSV file  
2. **C++ reads the CSV** and processes market conditions  
3. **Strategies analyze the data** and generate trading signals  
4. **Orders are sent to the OMS**, which manages execution via the broker  

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