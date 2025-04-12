#include "StrategyBase.hpp"

class RSI : public StrategyBase {
    public:
        RSI(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) 
        {
            
        }
        
        void execute() override 
        {
            validate();

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


        void reset()
        {
            NewOrder = false;
        }



        std::vector<float> getRecentCloses(int period)
        {
            std::vector<float> closes;
            const auto& data = marketData.getData();

            if (data.empty() || period <= 0) return closes;

            int start = std::max(0, static_cast<int>(data.size()) - period);

            for (int i = start; i < data.size(); ++i) {
                closes.push_back(data[i].Close);
            }
            return closes;
        }

        float calculateRSI(const std::vector<float>& closes)
        {

        }

        bool isOverbought(float rsi)
        {
            return (rsi > _strategyAttribute.overbought_threshold);
            // Returns true if RSI > overbought_threshold.
        }

        bool isOversold(float rsi)
        {
            return (rsi < _strategyAttribute.oversold_threshold);
            // Returns true if RSI < oversold_threshold.
        }

        bool hasOpenPosition(std::string ticker)
        {
            // Checks if there's already an open position for the ticker (via a broker or portfolio manager).
        }

        Order createBuyOrder(string ticker, float price, float quantity)
        {
            Order order{
                OrderType::BUY,
                ticker,
                quantity,
                price
            };

            NewOrder = true;
            return order;

            // Returns a properly configured buy order.
        }

        Order createBuyOrder(string ticker, float price, float quantity)
        {
            Order order{
                OrderType::SELL,
                ticker,
                quantity,
                price
            };

            NewOrder = true;
            return order;

            // Returns a properly configured buy order.
        }

        MarketCondition getCurrentMarketCondition()
        {
            // Retrieves the latest data point (probably from inside StrategyBase).
        }

        void logDecision(float rsi, std::string reason)
        {
            // Logs (or stores) the reason for a buy/sell/hold decision for testability and transparency.
        }

        StrategyAttribute getAttributes() { return _strategyAttribute; }
        
    private:
        double rsi;
        MarketData marketData;
};
