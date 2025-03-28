#include "BrokerBase.hpp"

class SimulatedBroker : public BrokerBase {
    public:
        SimulatedBroker();
        ~SimulatedBroker();

        int connect();
        int disconnect();
        float getLatestPrice(std::string ticker);
        void placeOrder(Order order);
        void getLatestPosition(std::string ticker);
        Position returnPosition(std::string ticker);
        
};
