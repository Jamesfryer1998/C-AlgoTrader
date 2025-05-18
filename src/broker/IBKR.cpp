#include "IBKR.hpp"
#include <unistd.h> 
#include <thread>



IBKR::IBKR() :
      m_osSignal(2000)//2-seconds timeout
    , m_pClient(new EClientSocket(this, &m_osSignal))
	, m_sleepDeadline(0)
	, m_orderId(0)
    , m_extraAuth(false)
{
}

IBKR::~IBKR()
{
    disconnect();
    
    // destroy the reader before the client
	if( m_pReader )
		m_pReader.reset();

	delete m_pClient;
}

void 
IBKR::SetUp()
{
    connected("127.0.0.1", PAPER_TRADING_PORT, 2);
    getTime();
}

int
IBKR::connected(const char* host, int port, int clientId)
{
    std::cout << "We are about to connect to IBKR broker" << std::endl;

    const char* resolvedHost = (host && *host) ? host : "127.0.0.1";
    std::cout << "Connecting to " << resolvedHost << ":" << port << " clientId:" << clientId << std::endl;

    int attempt = 0;
    bool bRes = false;
    while (attempt < MAX_CONNECTION_RETRY) 
    {
        bRes = m_pClient->eConnect(host, port, clientId, false);

        if(bRes)
        {
            break;
        }
        ++attempt;
    }

    if (bRes) {
        std::cout << "Connected to " << m_pClient->host()
                  << ":" << m_pClient->port()
                  << " clientId:" << clientId << std::endl;

        std::thread readerThread([&]() {
            m_pReader = std::make_unique<EReader>(m_pClient, &m_osSignal);
            m_pReader->start();

            while (m_pClient->isConnected()) {
                m_osSignal.waitForSignal();
                m_pReader->processMsgs();
            }
        });

        readerThread.detach();
        return 1;
    } else {
        std::cout << "Cannot connect to " << m_pClient->host()
                  << ":" << m_pClient->port()
                  << " clientId:" << clientId 
                  << " after num attepts: " << attempt << std::endl;
    }

    return 0;
}


int
IBKR::disconnect()
{
    m_pClient->eDisconnect();
    std::cout<<"We are disconnected from IBKR broker"<< std::endl;
    return 0;
}


float 
IBKR::getLatestPrice(std::string ticker)
{
    return 0;
}

int 
IBKR::placeOrder(oms::Order order)
{
    return 0;
}

oms::Position
IBKR::getLatestPosition(std::string ticker)
{
    return oms::Position();
}

void IBKR::getTime()
{
    std::cout << "Requesting Current Time" << std::endl;
    m_pClient->reqCurrentTime();
}