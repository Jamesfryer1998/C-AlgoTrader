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
    closes = [101.31999969482422, 101.57499694824219, 101.50499725341797, 101.49859619140625, 101.44000244140625, 101.51499938964844, 101.65499877929688, 101.57499694824219, 101.54000091552734, 101.46499633789062, 101.32499694824219, 101.37930297851562, 101.50890350341797, 101.30999755859375, 101.30999755859375, 101.2499008178711, 101.06500244140625, 101.02189636230469, 100.92500305175781, 100.98999786376953, 100.98979949951172, 100.94999694824219, 101.06999969482422, 100.97010040283203, 101.2300033569336, 101.3550033569336, 101.30989837646484, 101.26499938964844, 101.3499984741211, 101.41000366210938]

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