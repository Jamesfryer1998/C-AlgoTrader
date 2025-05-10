#include "IBKR.hpp"

IBKR::IBKR()
{
}

IBKR::~IBKR()
{
    disconnect();
}

void
IBKR::SetUp()
{
    // if(!connect())
        // std::runtime_error("Could not connect to IBKR broker after retries" + MAX_CONNECTION_RETRY);

}

int
// IBKR::connect(const char* host, int port, int clientId)
IBKR::connect()
{
    // std::cout<<"We are about to connect to IBKR broker"<< std::endl;

    // std::cout<<"We are connected to IBKR broker"<< std::endl;

	// // trying to connect
	// printf( "Connecting to %s:%d clientId:%d\n", !( host && *host) ? "127.0.0.1" : host, port, clientId);
	
	// //! [connect]
	// bool bRes = m_pClient->eConnect( host, port, clientId, false);
	// //! [connect]
	
	// if (bRes) {
	// 	printf( "Connected to %s:%d clientId:%d\n", m_pClient->host().c_str(), m_pClient->port(), clientId);
	// 	//! [ereader]
	// 	m_pReader = std::unique_ptr<EReader>( new EReader(m_pClient, &m_osSignal) );
	// 	m_pReader->start();
	// 	//! [ereader]
	// }
	// else
	// 	printf( "Cannot connect to %s:%d clientId:%d\n", m_pClient->host().c_str(), m_pClient->port(), clientId);

	// // return bRes;
    return 1;
}

int
IBKR::disconnect()
{
    std::cout<<"We are disconnected from IBKR broker"<< std::endl;
    return 0;
}



float 
IBKR::getLatestPrice(std::string ticker)
{
    return 0;
}

int 
IBKR::placeOrder(Order order)
{
    return 0;
}

Position
IBKR::getLatestPosition(std::string ticker)
{
    return Position();
}
