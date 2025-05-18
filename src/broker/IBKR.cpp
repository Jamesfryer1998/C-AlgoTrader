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
    if (connected("127.0.0.1", PAPER_TRADING_PORT, 2)) {
        std::cout << "[SetUp] Connected. Waiting for nextValidId..." << std::endl;
    } else {
        std::cerr << "[SetUp] Failed to connect to IBKR" << std::endl;
    }
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


float IBKR::getLatestPrice(std::string ticker)
{
    Contract contract;
    contract.symbol = "EUR";
    contract.secType = "CASH";
    contract.currency = "GBP";
    contract.exchange = "IDEALPRO";

    TickerId tickerId;
    {
        std::lock_guard<std::mutex> lock(m_priceMutex);
        if (m_tickerToId.find(ticker) == m_tickerToId.end()) {
            tickerId = m_nextTickerId++;
            m_tickerToId[ticker] = tickerId;
        } else {
            tickerId = m_tickerToId[ticker];
        }
    }

    m_pClient->reqMktData(tickerId, contract, "", false, false, TagValueListSPtr());

    int waitTimeMs = 5000;
    int stepMs = 100;
    int waited = 0;
    while (waited < waitTimeMs) 
    {
        if (m_lastPrices.count(tickerId) > 0) {
            double price = m_lastPrices[tickerId];
            if (price > 0.0) {  // skip -1.0 and invalid
                std::cout << "[getLatestPrice] Got price: " << price << std::endl;
                return static_cast<float>(price);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
        waited += stepMs;
    }

    std::cout << "[getLatestPrice] Price not received in time for ticker: " << ticker << std::endl;
    return -1.0f;
}



int IBKR::placeOrder(oms::Order order)
{
    Contract contract;
    contract.symbol = "AAPL";
    contract.secType = "STK";
    contract.exchange = "SMART";
    contract.currency = "USD";


    Order ibOrder;
    ibOrder.action = "BUY";
    ibOrder.totalQuantity = 1;
    ibOrder.orderType = "MKT";

    // You might want to set these too:
    ibOrder.transmit = true;

    int currentOrderId = m_orderId++;  // Assign unique order ID

    std::cout << "Placing order " << currentOrderId << " for " << "AAPL"
              << " (" << ibOrder.action << " " << 1 << ")" << std::endl;

    m_pClient->placeOrder(currentOrderId, contract, ibOrder);

    return currentOrderId;
}

oms::Position
IBKR::getLatestPosition(std::string ticker)
{
    return oms::Position();
}

void IBKR::getCurrentTime()
{
    std::cout << "Requesting Current Time" << std::endl;
    m_pClient->reqCurrentTime();
}




// ******************** EWrapper Overrides ********************
// These methods help us display what is recieved from TWS API
void
IBKR::currentTime(long time)
{
    std::cout << "Time: " << time << std::endl;
}

void IBKR::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attribs)
{

    if (field == TickType::LAST || field == TickType::CLOSE || field == TickType::ASK || field == TickType::BID) {
        std::lock_guard<std::mutex> lock(m_priceMutex);
        m_lastPrices[tickerId] = price;
        std::cout << "tickPrice: TickerId=" << tickerId 
                << ", field=" << field 
                << ", price=" << price << std::endl;
    }
}

void IBKR::tickSize(TickerId tickerId, TickType field, Decimal size) {
    // std::cout << "[tickSize] id=" << tickerId << ", field=" << field << ", size=" << size << std::endl;
}

void IBKR::tickString(TickerId tickerId, TickType field, const std::string& value) {
    std::cout << "[tickString] id=" << tickerId << ", field=" << field << ", value=" << value << std::endl;
}

void IBKR::tickGeneric(TickerId tickerId, TickType field, double value) {
    std::cout << "[tickGeneric] id=" << tickerId << ", field=" << field << ", value=" << value << std::endl;
}

void IBKR::orderStatus(OrderId orderId, const std::string& status, Decimal filled,
                       Decimal remaining, double avgFillPrice, int permId,
                       int parentId, double lastFillPrice, int clientId,
                       const std::string& whyHeld, double mktCapPrice)
{
    std::cout << "Order #" << orderId << " status: " << status
              << ", filled: " << filled << " avg price: " << avgFillPrice << std::endl;
}

void IBKR::error(int id, int code, const std::string& msg, const std::string& json) {
    if(id != -1)
        std::cout << "[ERROR] id=" << id << ", code=" << code << ", msg=" << msg << std::endl;
}

void IBKR::connectionClosed() {
    std::cout << "[CONNECTION CLOSED]" << std::endl;
}

void IBKR::nextValidId(OrderId orderId)
{
    std::cout << "[nextValidId] Received Order ID: " << orderId << std::endl;
    m_orderId = orderId;

    // Now it’s safe to send API requests
    getCurrentTime();
    std::thread([this]() {
        float price = this->getLatestPrice("IBKR");
        std::cout << "[nextValidId] IBKR price: " << price << std::endl;
    }).detach();

    // Uncomment to place order
    oms::Order order;
    placeOrder(order);
}