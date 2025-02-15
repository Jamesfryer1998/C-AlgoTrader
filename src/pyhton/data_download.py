import os
import json
import pandas as pd
import yfinance as yf
from datetime import datetime, timedelta

class DataDownload:
    def __init__(self):
        config_file_path = "src/config/algo_trader.json"
        config_data = self.load_json(config_file_path)
        self.base_data_dir = config_data["marketDataBasePath"]
        self.data_base_file_name = config_data["baseDataFileName"]
        self.ticker = config_data["ticker"]
        self.interval = config_data["collectInterval"]
        date = datetime.now().date()
        self.date_file_path = f"{self.base_data_dir}{self.data_base_file_name}_{self.ticker}_{date}.csv"

    def get_stock_data(self, ticker):
        """
        Fetches historical stock data for the past week.
        
        :param ticker: Stock symbol (e.g., "AAPL").
        :param interval: Interval for data (e.g., "1m", "5m", "1h", "1d").
        :info: Defaults to 1h interval
        :return: Pandas DataFrame with historical stock data.
        """
        print(f"About to fetch data for {ticker}...")
        end_date = datetime.today().strftime('%Y-%m-%d')
        start_date = (datetime.today() - timedelta(days=7)).strftime('%Y-%m-%d')

        stock = yf.Ticker(ticker)
        data = stock.history(interval=self.interval, start=start_date, end=end_date)
        print(f"Retrieved [{len(data)}] rows for {ticker} with [{self.interval}] interval.")
        return data
    
    def save_csv(self):
        """Saves stock data to a CSV file, ensuring the base directory exists and normalizing timestamps."""
        # Ensure the directory exists
        base_dir = os.path.dirname(self.date_file_path)
        os.makedirs(base_dir, exist_ok=True)

        # Fetch stock data
        data = pd.DataFrame(self.get_stock_data(self.ticker), columns=["Ticker", "Open", "Close", "Volume", "TimeInterval"])
        data["Ticker"] = self.ticker
        data["TimeInterval"] = self.interval
        data.index = pd.to_datetime(data.index).tz_localize(None) 
        data.fillna(0) 
        data.to_csv(self.date_file_path, index=True, sep=",")
        print(f"Data saved to {self.date_file_path}")
            
    def load_json(self, file_path):
        """
        Loads a JSON file and returns its contents as a dictionary.
        
        :param file_path: Path to the JSON file.
        :return: Dictionary with the JSON contents.
        """
        try:
            with open(file_path, "r") as file:
                data = json.load(file)
            return data
        except FileNotFoundError:
            print(f"Error: File '{file_path}' not found.")
            return None
        except json.JSONDecodeError:
            print(f"Error: Failed to decode JSON in '{file_path}'.")
            return None        