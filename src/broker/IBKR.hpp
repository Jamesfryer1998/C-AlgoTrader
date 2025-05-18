#include "BrokerBase.hpp"
#include "IBKRStubs.hpp"
#include "EClientSocket.h"
#include "EReader.h"
#include "EWrapper.h"
#include "EReaderOSSignal.h"
#include "EPosixClientSocketPlatform.h"
#include "CommonDefs.h"
#include "TickAttrib.h"

#define MAX_CONNECTION_RETRY 5
#define RETRY_DELAY_SECONDS 2
#define LIVE_TRADING_PORT 7496
#define PAPER_TRADING_PORT 7497

class IBKR : public BrokerBase, IBKRStubs
{
    public:
        IBKR();
        ~IBKR();

        void SetUp();


    private:
        int connect(){ return 1;};
        int connected(const char* host, int port, int clientId);
        int disconnect();
        float getLatestPrice(std::string ticker);
        int placeOrder(oms::Order order);
        oms::Position getLatestPosition(std::string ticker);

		void getTime();

        //! [socket_declare]
        EReaderOSSignal m_osSignal;
        EClientSocket * const m_pClient;
        //! [socket_declare]
        time_t m_sleepDeadline;

        OrderId m_orderId;
        std::unique_ptr<EReader> m_pReader;
        bool m_extraAuth;
        std::string m_bboExchange;

};
