import numpy as np
import pandas as pd
import talib
import yfinance as yf

def analyze_rsi(closes, period=29, overbought=70, oversold=30):

    # Convert list to numpy array if it's not already
    closes_array = np.array(closes, dtype=np.float64)
    
    # Create DataFrame with closing prices
    df = pd.DataFrame({'Close': closes_array})
    
    # Calculate RSI using talib
    df['RSI'] = talib.RSI(closes_array, timeperiod=period)
    
    # Generate trading signals
    df['Signal'] = 0  # 0 means no signal
    df.loc[df['RSI'] < oversold, 'Signal'] = 1  # 1 means buy
    df.loc[df['RSI'] > overbought, 'Signal'] = -1  # -1 means sell
    
    # Drop rows with NaN RSI values (initial periods)
    df = df.dropna()
    
    return df

# Example usage
if __name__ == "__main__":
    # Example list of closing prices
    closes = [101.32, 101.57, 101.50, 101.50, 101.44, 101.51, 101.65, 101.57, 101.54, 101.46, 101.32, 101.38, 101.51, 101.31, 101.31, 101.25, 101.07, 101.02, 100.93, 100.99, 100.99, 100.95, 101.07, 100.97, 101.23, 101.36, 101.31, 101.26, 101.35, 101.41]

    print(len(closes))
    
    # Analyze with RSI
    data = analyze_rsi(closes)
    
    # Print the results
    print("RSI Analysis:")
    print(data)
    
    # Current RSI position
    current_rsi = data['RSI'].iloc[-1]
    print(f"\nLatest RSI: {current_rsi:.2f}")
    
    if current_rsi < 30:
        print("Current status: OVERSOLD (Potential Buy)")
    elif current_rsi > 70:
        print("Current status: OVERBOUGHT (Potential Sell)")
    else:
        print("Current status: NEUTRAL")