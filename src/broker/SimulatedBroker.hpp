#include "BrokerBase.hpp"
#include "../data_access/MarketData.hpp"

class SimulatedBroker : public BrokerBase {
    public:
        SimulatedBroker(MarketData marketdata);
        ~SimulatedBroker();

        int connect();
        int disconnect();
        float getLatestPrice(std::string ticker);
        void placeOrder(Order order);
        Position getLatestPosition(std::string ticker);
        void nextStep();
        std::string getBrokerName() { return brokerName; };
        void process();

    private:
        MarketCondition currentCondition;
        MarketData marketData;
        std::vector<Order> orders;
        std::vector<Position> positions;
        float latestPrice;
        int step;
        std::string simulationTime;


};
