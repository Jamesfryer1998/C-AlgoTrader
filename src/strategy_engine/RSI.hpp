#include "StrategyBase.hpp"
#include <cassert>
#include <iomanip> // For std::setprecision

class RSI : public StrategyBase {
    public:
        RSI(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) 
        {
            
        }
        
        void execute() override 
        {
            validate();
            run();
        }

        void supplyData(MarketData& marketdata) override
        {
            marketData = marketdata;
        }

        MarketData getData()
        {
            return marketData;
        }

        float calculateRSI(const std::vector<float>& closes)
        {
            // Verify we have enough data to calculate RSI properly
            if (closes.size() <= 1) {
                // Since getRecentCloses() now strictly enforces the period requirement,
                // we should never get here unless there's an error
                std::cout << "ERROR - calculateRSI: Called with insufficient data (" 
                          << closes.size() << " points)" << std::endl;
                return 50.0f; // Return neutral value as a fallback
            }

            float gainSum = 0.0f;
            float lossSum = 0.0f;
            int period = _strategyAttribute.period;
            
            assert(static_cast<int>(closes.size()) >= period);
            
            // Calculate gains and losses for all available price changes
            for (size_t i = 1; i < closes.size(); i++) {
                float change = closes[i] - closes[i-1];
                if (change > 0) {
                    gainSum += change;
                } else if (change < 0) {
                    lossSum -= change;
                }
            }
            
            // Use the configured period for the average
            float avgGain = gainSum / (closes.size() - 1);
            float avgLoss = lossSum / (closes.size() - 1);
            
            // Handle edge cases
            if (avgLoss == 0.0f && avgGain == 0.0f) {
                std::cout << "INFO - calculateRSI: No price movement, returning neutral RSI 50" << std::endl;
                return 50.0f;
            } else if (avgLoss == 0.0f) {
                std::cout << "INFO - calculateRSI: No losses, returning maximum RSI 100" << std::endl;
                return 100.0f;
            }
            
            float rs = avgGain / avgLoss;
            float rsi = 100.0f - (100.0f / (1.0f + rs));
            
            std::cout << "INFO - calculateRSI: Calculated RSI(" << period << "): " << std::fixed 
                      << std::setprecision(2) << rsi << " using " << closes.size() << " data points" << std::endl;
            
            return std::round(rsi * 100) / 100;
        }

    private:

        void validate() override
        {
            if(!_strategyAttribute.period)
                throw std::runtime_error("RSI period not set");
            if(!_strategyAttribute.overbought_threshold)
                throw std::runtime_error("RSI overbought threshold not set");
            if(!_strategyAttribute.oversold_threshold)
                throw std::runtime_error("RSI oversold threshold not set");
        }

        void run()
        {
            std::vector<float> recentCloses = getRecentCloses(_strategyAttribute.period);
            if(recentCloses.size() == 0) {
                std::cout << "DEBUG - RSI::run - No recent closes, skipping RSI calculation" << std::endl;
                return;
            }
            
            std::cout << "DEBUG - RSI::run - Got " << recentCloses.size() << " recent closes" << std::endl;
            MarketCondition currentCondition = getCurrentMarketCondition();
            float quantity = 1;

            rsi = calculateRSI(recentCloses);

            if (isOverbought())
            {
                order = createSellOrder(currentCondition.Ticker, currentCondition.Close, quantity);
            }
            else if (isOversold())
            {
                order = createBuyOrder(currentCondition.Ticker, currentCondition.Close, quantity);
            }
            else if (isNeutral())
            {
                holdOrder();
            }

            if (decision != orderTypeToString(OrderType::HOLD))
                logDecision(currentCondition, rsi, quantity);
        }

        std::vector<float> getRecentCloses(int period)
        {
            std::vector<float> closes;
            const auto& data = marketData.getData();
            int dataSize = static_cast<int>(data.size());
            
            if (data.empty() || period <= 0) 
            {
                std::cout << "Data is empty, or invalid period set, period:" << period << std::endl;
                return closes;
            }
            
            if (dataSize < period)
            {
                std::cout << "Skipping RSI calculation: Insufficient data (" << dataSize 
                          << " points available, " << period << " required)" << std::endl;
                return closes; // Return empty vector
            }

            int start = std::max(0, dataSize - period);
            // std::cout << "DEBUG: Recent closes for RSI calculation:" << std::endl;
            for (int i = start; i < dataSize; ++i) {
                closes.push_back(data[i].Close);
                // std::cout << "  [" << i - start << "] " << data[i].DateTime << ": $" 
                //           << std::fixed << std::setprecision(2) << data[i].Close << std::endl;
            }
            return closes;
        }


        bool isOverbought()
        {
            return (rsi > _strategyAttribute.overbought_threshold);
        }

        bool isOversold()
        {
            return (rsi < _strategyAttribute.oversold_threshold);
        }

        bool isNeutral()
        {
            return (rsi < _strategyAttribute.overbought_threshold && rsi > _strategyAttribute.oversold_threshold);
        }

        Order createBuyOrder(string ticker, float price, float quantity)
        {
            Order order{
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

        Order createSellOrder(string ticker, float price, float quantity)
        {
            Order order{
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

        void logDecision(MarketCondition currentCondition, float rsi, float quantity)
        {
            std::cout << "\n==== RSI SIGNAL ====" << std::endl;
            std::cout << "Date: " << currentCondition.DateTime << std::endl;
            std::cout << "Ticker: " << currentCondition.Ticker << std::endl;
            std::cout << "Price:  $" << std::fixed << std::setprecision(2) << currentCondition.Close << std::endl;
            std::cout << "Quantity:  " << std::fixed << std::setprecision(2) << quantity << std::endl;
            std::cout << "RSI:    " << std::fixed << std::setprecision(2) << rsi << std::endl;
            std::cout << "Action: " << decision << std::endl;
            std::cout << "===================\n" << std::endl;
        }

        StrategyAttribute getAttributes() { return _strategyAttribute; }
        
    private:
        float rsi;
        string decision;
        MarketData marketData;
};
