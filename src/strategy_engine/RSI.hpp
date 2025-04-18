#include "StrategyBase.hpp"

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

        void supplyData(MarketData marketdata) override
        {
            marketData = marketdata;
        }

        MarketData getData()
        {
            return marketData;
        }

        float calculateRSI(const std::vector<float>& closes)
        {
            float gainSum = 0.0f;
            float lossSum = 0.0f;
            int period = _strategyAttribute.period;   

            
            // Calculate gains and losses for the specified period
            for (size_t i = closes.size() - period; i < closes.size()-1; i++) {
                float current = closes[i];
                float next = closes[i+1];
                float change = next-current;
                if (change > 0) {
                    gainSum += change;
                } else {
                    lossSum -= change;
                }
            }
            
            // Calculate average gain and loss
            float avgGain = gainSum / period;
            float avgLoss = lossSum / period;
            
            // Handle division by zero
            if (avgLoss == 0.0f && avgGain == 0.0f) {
                return 50.0f; // No movement, neutral RSI
            } else if (avgLoss == 0.0f) {
                return 100.0f; // No losses means RSI = 100
            }
            
            float rs = avgGain / avgLoss;
            float rsi = 100.0f - (100.0f / (1.0f + rs));
            
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
            if(recentCloses.size() == 0) return;
            MarketCondition currentCondition = getCurrentMarketCondition();
            float quantity = 0.0;

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
                logDecision(currentCondition, rsi);
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
                std::cout << "Not enought data for period: " << period << std::endl;
                return closes;
            }

            int start = std::max(0, dataSize - period);

            for (int i = start; i < dataSize; ++i) {
                closes.push_back(data[i].Close);
            }

            // for (int i = dataSize-1; i > dataSize-period; i--){
            //     closes.push_back(data[i].Close);
            // }
            return closes;
        }


        bool isOverbought()
        {
            return (rsi > _strategyAttribute.overbought_threshold);
            // Returns true if RSI > overbought_threshold.
        }

        bool isOversold()
        {
            return (rsi < _strategyAttribute.oversold_threshold);
            // Returns true if RSI < oversold_threshold.
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

        void logDecision(MarketCondition currentCondition, float rsi)
        {
            std::cout << "\n==== RSI SIGNAL ====" << std::endl;
            std::cout << "Date: " << currentCondition.DateTime << std::endl;
            std::cout << "Ticker: " << currentCondition.Ticker << std::endl;
            std::cout << "Price:  $" << std::fixed << std::setprecision(2) << currentCondition.Close << std::endl;
            std::cout << "RSI:    " << std::fixed << std::setprecision(1) << rsi << std::endl;
            std::cout << "Action: " << decision << std::endl;
            std::cout << "===================\n" << std::endl;
        }

        StrategyAttribute getAttributes() { return _strategyAttribute; }
        
    private:
        float rsi;
        string decision;
        MarketData marketData;
};
