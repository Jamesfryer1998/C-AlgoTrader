#include "../oms/Order.hpp"
#include "StrategyBase.hpp"

struct MACDResult {
    float macdLine;
    float signalLine;
    float histogram;
};

class MACD : public StrategyBase {
    public:
        MACD(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) {}

        void execute() override 
        {
            validate();
            run();
        }

        void validate() override
        {
            if(!_strategyAttribute.short_period)
                throw std::runtime_error("MACD short_period not set");
            if(!_strategyAttribute.long_period)
                throw std::runtime_error("MACD long_period not set");
            if(!_strategyAttribute.signal_period)
                throw std::runtime_error("MACD signal_period not set");
        } 

        void run()
        {
            std::vector<float> recentCloses = getCloses();
            if(recentCloses.size() == 0) {
                std::cout << "DEBUG - MACD::run - No recent closes, skipping MACD calculation" << std::endl;
                return;
            }
            
            // Need at least enough data for long period + signal period
            size_t minDataPoints = static_cast<size_t>(_strategyAttribute.long_period + _strategyAttribute.signal_period);
            if(recentCloses.size() < minDataPoints) {
                std::cout << "DEBUG - MACD::run - Not enough data points. Need " << minDataPoints 
                         << ", have " << recentCloses.size() << std::endl;
                return;
            }
            
            std::cout << "DEBUG - MACD::run - Got " << recentCloses.size() << " recent closes" << std::endl;
            MarketCondition currentCondition = getCurrentMarketCondition();
            float quantity = 1;

            // Calculate MACD values
            std::vector<MACDResult> macdResults = calculateMACD(recentCloses);
            if(macdResults.size() < 2) {
                std::cout << "DEBUG - MACD::run - Not enough MACD results for signal generation" << std::endl;
                return;
            }

            // Get current and previous MACD values for crossover detection
            MACDResult current = macdResults.back();
            MACDResult previous = macdResults[macdResults.size() - 2];

            // Check for signals
            if (isBullishCrossover(previous, current) || isBullishZeroCross(previous, current))
            {
                order = createBuyOrder(currentCondition.Ticker, currentCondition.Close, quantity);
            }
            else if (isBearishCrossover(previous, current) || iBearishZeroCross(previous, current))
            {
                order = createSellOrder(currentCondition.Ticker, currentCondition.Close, quantity);
            }
            else
            {
                holdOrder();
            }

            if (decision != orderTypeToString(OrderType::HOLD))
                logDecision(currentCondition, current, quantity);
        }

        std::vector<float> getCloses()
        {
            std::vector<float> closes;
            const auto& data = marketData.getData();
            int dataSize = static_cast<int>(data.size());
            
            if (data.empty()) 
            {
                std::cout << "Data is empty " << std::endl;
                return closes;
            }
            
            for (int i = 0; i < dataSize; ++i) {
                closes.push_back(data[i].Close);
            }
            return closes;
        }

        std::vector<MACDResult> calculateMACD(const std::vector<float>& closes)
        {
            std::vector<MACDResult> results;
            
            // Calculate EMAs
            std::vector<float> shortEMA = calculateEMA(closes, _strategyAttribute.short_period);
            std::vector<float> longEMA = calculateEMA(closes, _strategyAttribute.long_period);
            
            // Calculate MACD line (difference between short and long EMAs)
            std::vector<float> macdLine;
            int startIndex = _strategyAttribute.long_period - 1; // Start when we have valid long EMA
            
            for (size_t i = startIndex; i < closes.size(); ++i) {
                float macdValue = shortEMA[i] - longEMA[i];
                macdLine.push_back(macdValue);
            }
            
            // Calculate signal line (EMA of MACD line)
            std::vector<float> signalLine = calculateEMA(macdLine, _strategyAttribute.signal_period);
            
            // Calculate histogram (MACD - Signal)
            int signalStartIndex = _strategyAttribute.signal_period - 1;
            for (size_t i = signalStartIndex; i < macdLine.size(); ++i) {
                MACDResult result;
                result.macdLine = macdLine[i];
                result.signalLine = signalLine[i];
                result.histogram = result.macdLine - result.signalLine;
                results.push_back(result);
            }
            
            return results;
        }

        std::vector<float> calculateEMA(const std::vector<float>& prices, int period)
        {
            std::vector<float> ema(prices.size());
            if (prices.empty() || period <= 0) return ema;
            
            float multiplier = 2.0f / (period + 1);
            
            // First EMA value is just the first price
            ema[0] = prices[0];
            
            // Calculate subsequent EMA values
            for (size_t i = 1; i < prices.size(); ++i) {
                ema[i] = (prices[i] * multiplier) + (ema[i-1] * (1 - multiplier));
            }
            
            return ema;
        }

        // Crossover Strategy Methods
        bool isBullishCrossover(const MACDResult& previous, const MACDResult& current)
        {
            // Signal line crosses below MACD line (MACD goes above signal)
            return (previous.macdLine <= previous.signalLine) && (current.macdLine > current.signalLine);
        }

        bool isBearishCrossover(const MACDResult& previous, const MACDResult& current)
        {
            // MACD line crosses below signal line
            return (previous.macdLine >= previous.signalLine) && (current.macdLine < current.signalLine);
        }

        // Zero-Cross Strategy Methods
        bool isBullishZeroCross(const MACDResult& previous, const MACDResult& current)
        {
            // MACD crosses from below to above zero line
            return (previous.macdLine <= 0) && (current.macdLine > 0);
        }

        bool iBearishZeroCross(const MACDResult& previous, const MACDResult& current)
        {
            // MACD crosses from above to below zero line
            return (previous.macdLine >= 0) && (current.macdLine < 0);
        }

        oms::Order createBuyOrder(string ticker, float price, float quantity)
        {
            oms::Order order{
                OrderType::BUY,
                ticker,
                quantity,
                price
            };

            order.setStopLoss(_strategyAttribute.stop_loss);
            order.setTakeProfit(_strategyAttribute.take_profit);

            decision = orderTypeToString(OrderType::BUY);
            NewOrder = true;
            return order;
        }

        oms::Order createSellOrder(string ticker, float price, float quantity)
        {
            oms::Order order{
                OrderType::SELL,
                ticker,
                quantity,
                price
            };

            order.setStopLoss(_strategyAttribute.stop_loss);
            order.setTakeProfit(_strategyAttribute.take_profit);

            decision = orderTypeToString(OrderType::SELL);
            NewOrder = true;
            return order;
        }

        void holdOrder()
        {
            decision = orderTypeToString(OrderType::HOLD);
        }

        MarketCondition getCurrentMarketCondition()
        {
            return marketData.getCurrentData();
        }

        void supplyData(MarketData& marketdata) override
        {
            marketData = marketdata;
        }
        
        MarketData getData()
        {
            return marketData;
        }

        void logDecision(MarketCondition currentCondition, const MACDResult& macdResult, float quantity)
        {
            std::cout << "\n==== MACD SIGNAL ====" << std::endl;
            std::cout << "Date: " << currentCondition.DateTime << std::endl;
            std::cout << "Ticker: " << currentCondition.Ticker << std::endl;
            std::cout << "Price:  $" << std::fixed << std::setprecision(2) << currentCondition.Close << std::endl;
            std::cout << "Quantity:  " << std::fixed << std::setprecision(2) << quantity << std::endl;
            std::cout << "MACD Line:    " << std::fixed << std::setprecision(4) << macdResult.macdLine << std::endl;
            std::cout << "Signal Line:  " << std::fixed << std::setprecision(4) << macdResult.signalLine << std::endl;
            std::cout << "Histogram:    " << std::fixed << std::setprecision(4) << macdResult.histogram << std::endl;
            std::cout << "Action: " << decision << std::endl;
            std::cout << "===================\n" << std::endl;
        }

        StrategyAttribute getAttributes(){ return _strategyAttribute; }   

    private:
        string decision;
        MarketData marketData;
};