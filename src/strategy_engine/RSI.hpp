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
            // std::cout << "  -> RSI" << std::endl;
            // createOrder(order);
        }

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

            logDecision(currentCondition.Ticker, currentCondition.Close);
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
            return closes;
        }

        float calculateRSI(const std::vector<float>& closes)
        {
            return 0.0;
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
            // Retrieves the latest data point (probably from inside StrategyBase).
        }

        void logDecision(string ticker, float price)
        {
            std::cout << "We have a " << decision << " for ticker: " << ticker << " at price: " << price << endl;
        }

        StrategyAttribute getAttributes() { return _strategyAttribute; }
        
    private:
        float rsi;
        string decision;
        MarketData marketData;
};
