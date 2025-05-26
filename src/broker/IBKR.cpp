#include "IBKR.hpp"
#include <unistd.h> 
#include <thread>
#include <iomanip>



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

int 
IBKR::verifyTradingPort(json configData)
{
    int port = PAPER_TRADING_PORT;

    if(configData["IBKR_Trading_Type"] == "PAPER")
        port = PAPER_TRADING_PORT;
    else if(configData["IBKR_Trading_Type"] == "LIVE")
    {
        port = LIVE_TRADING_PORT;

        std::cerr << " ************************** [WARNING] **************************" << std::endl;
        std::cerr << "You are attempting to connect to the LIVE trading environment." << std::endl;
        std::cerr << "This will execute REAL trades. Do you want to continue? (y/n): ";

        std::string response;
        std::getline(std::cin, response);

        if (response != "y" && response != "Y")
        {
            throw std::runtime_error("Aborted: User chose not to proceed with LIVE trading. Please change to PAPER trading.");
        }
    }
    
    return port;
}

void 
IBKR::SetUp(json configData)
{
    int port = verifyTradingPort(configData);
    
    if (connected("127.0.0.1", port, 2)) {
        std::cout << "[SetUp] Connected. Waiting for nextValidId..." << std::endl;
        m_state = ST_CONNECTED;
    } else {
        std::cerr << "[SetUp] Failed to connect to IBKR" << std::endl;
        if(port == LIVE_TRADING_PORT)
            std::cerr << "[SetUp] Check you are connected to correct IBKR PAPER/LIVE" << std::endl;
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

    m_state = ST_DISCONNECTED;
    return 0;
}

Contract IBKR::buildCurrencyContract(const std::string& symbol) 
{
    Contract contract;
    contract.symbol = symbol;
    contract.secType = "CASH";
    contract.currency = "GBP";
    contract.exchange = "IDEALPRO";
    return contract;
}

Contract IBKR::buildStockContract(const std::string& symbol) 
{
    Contract contract;
    contract.symbol = symbol;
    contract.secType = "STK";
    contract.currency = "USD";
    contract.exchange = "SMART";
    return contract;
}


double IBKR::getLatestPrice(std::string ticker)
{
    std::unique_lock<std::mutex> lock(m_readyMutex);
    if (!m_isReady) {
        std::cout << "[getLatestPrice] Waiting for nextValidId..." << std::endl;
        m_readyCond.wait(lock, [this] { return m_isReady; });
    }

    // Now it's safe to send requests
    Contract contract;

    if (ticker == "EURGBP") {
        contract = buildCurrencyContract("EUR");
    } else {
        contract = buildStockContract(ticker);
    }

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
    m_state = ST_REQMKTDATA;

    int waitTimeMs = 5000;
    int stepMs = 100;
    int waited = 0;
    while (waited < waitTimeMs) {
        if (m_lastPrices.count(tickerId) > 0) {
            double price = m_lastPrices[tickerId];
            if (price > 0.0) {
                std::cout << "[getLatestPrice] Got price: " << price << std::endl;
                cancelMarketData(tickerId);
                return price;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
        waited += stepMs;
    }

    std::cout << "[getLatestPrice] Price not received in time for ticker: " << ticker << std::endl;
    return -1.0;
}


void IBKR::cancelMarketData(TickerId tickerId)
{
    {
        std::lock_guard<std::mutex> lock(m_priceMutex);
        m_lastPrices.erase(tickerId);
        m_activeMarketData.erase(tickerId);
    }

    std::cout << "[cancelMarketData] Cancelling market data for TickerId: " << tickerId << std::endl;
    m_pClient->cancelMktData(tickerId);
}


int IBKR::placeOrder(oms::Order order)
{
    std::unique_lock<std::mutex> lock(m_readyMutex);
    if (!m_isReady) {
        std::cout << "[placeOrder] Waiting for nextValidId..." << std::endl;
        m_readyCond.wait(lock, [this] { return m_isReady; });
    }
    
    Contract contract;
    contract.symbol = "EUR";              // Base currency
    contract.secType = "CASH";
    contract.currency = "GBP";           // Quote currency
    contract.exchange = "IDEALPRO";

    Order ibOrder;
    ibOrder.action = "BUY";              // Buy EUR (Sell GBP)
    ibOrder.totalQuantity = Decimal(100.0);        // e.g., buy 1000 EUR
    ibOrder.orderType = "MKT";
    ibOrder.transmit = true;

    int currentOrderId = m_orderId++;    // Assign unique order ID

    std::cout << "Placing order " << currentOrderId << " for EUR/GBP"
              << " (" << ibOrder.action << " " << ibOrder.totalQuantity << ")" << std::endl;

    m_pClient->placeOrder(currentOrderId, contract, ibOrder);

    m_state = ST_ORDERPLACED;

    return currentOrderId;
}

void
IBKR::syncPositions()
{
    
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
    m_state = ST_CURRENTTIME;
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
        std::cout << std::fixed << std::setprecision(5)
                << "tickPrice: TickerId=" << tickerId 
                << ", field=" << field 
                << ", price=" << price << std::endl;
    }
}

void IBKR::tickSize(TickerId tickerId, TickType field, Decimal size) {
    // std::cout << "[tickSize] id=" << tickerId << ", field=" << field << ", size=" << size << std::endl;
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
    
    std::string stateString = ConvertStateToString();
    if(id != -1)
        std::cout << "[ERROR] id=" << id << ", code=" << code <<  ", state=" << stateString << ", msg=" << msg << std::endl;
}

void IBKR::connectionClosed() {
    std::cout << "[CONNECTION CLOSED]" << std::endl;
}

void IBKR::nextValidId(OrderId orderId)
{
    std::cout << "[nextValidId] Received Order ID: " << orderId << std::endl;
    m_orderId = orderId;

    {
        std::lock_guard<std::mutex> lock(m_readyMutex);
        m_isReady = true;
    }
    m_readyCond.notify_all();
}

std::string
IBKR::ConvertStateToString()
{
    switch (m_state) {
        case ST_CONNECTED:
            return "ST_CONNECTED";
        case ST_DISCONNECTED:
            return "ST_DISCONNECTED";
        case ST_REQMKTDATA:
            return "ST_REQMKTDATA";
        case ST_ORDERPLACED:
            return "ST_ORDERPLACED";
        case ST_CURRENTTIME:
            return "ST_CURRENTTIME";
        default:
            return "UNKNOWN_STATE";
    }
}