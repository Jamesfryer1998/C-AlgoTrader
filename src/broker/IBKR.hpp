#include "BrokerBase.hpp"
#include "EClientSocket.h"
#include "EReader.h"
#include "EWrapper.h"
#include "EReaderOSSignal.h"
#include "EPosixClientSocketPlatform.h"
#include "CommonDefs.h"
#include "TickAttrib.h"
#include "Order.h"

#define MAX_CONNECTION_RETRY 5
#define RETRY_DELAY_SECONDS 2
#define LIVE_TRADING_PORT 7496
#define PAPER_TRADING_PORT 7497

enum State {
	ST_CONNECTED,
    ST_DISCONNECTED,
    ST_REQMKTDATA,
    ST_ORDERPLACED,
    ST_CURRENTTIME
};

class IBKR : public BrokerBase, EWrapper
{
    public:
        IBKR();
        ~IBKR();

        void SetUp();

        // Ewrapper abstract override
        void tickPrice(TickerId, TickType, double, const TickAttrib&) override;
        void tickSize(TickerId, TickType, Decimal) override;
        void tickOptionComputation(TickerId, TickType, int, double, double, double, double, double, double, double, double) override {}
        void tickGeneric(TickerId, TickType, double) override;
        void tickString(TickerId, TickType, const std::string&) override;
        void tickEFP(TickerId, TickType, double, const std::string&, double, int, const std::string&, double, double) override {}
        void orderStatus(OrderId, const std::string&, Decimal, Decimal, double, int, int, double, int, const std::string&, double) override;
        void openOrder(OrderId, const Contract&, const Order&, const OrderState&) override {}
        void openOrderEnd() override {}
        void winError(const std::string&, int) override {}
        void connectionClosed() override;
        void updateAccountValue(const std::string&, const std::string&, const std::string&, const std::string&) override {}
        void updatePortfolio(const Contract&, Decimal, double, double, double, double, double, const std::string&) override {}
        void updateAccountTime(const std::string&) override {}
        void accountDownloadEnd(const std::string&) override {}
        void nextValidId(OrderId) override;
        void contractDetails(int, const ContractDetails&) override {}
        void bondContractDetails(int, const ContractDetails&) override {}
        void contractDetailsEnd(int) override {}
        void execDetails(int, const Contract&, const Execution&) override {}
        void execDetailsEnd(int) override {}
        void error(int, int, const std::string&, const std::string&) override;
        void updateMktDepth(TickerId, int, int, int, double, Decimal) override {}
        void updateMktDepthL2(TickerId, int, const std::string&, int, int, double, Decimal, bool) override {}
        void updateNewsBulletin(int, int, const std::string&, const std::string&) override {}
        void managedAccounts(const std::string&) override {}
        void receiveFA(faDataType, const std::string&) override {}
        void historicalData(TickerId, const Bar&) override {}
        void historicalDataEnd(int, const std::string&, const std::string&) override {}
        void scannerParameters(const std::string&) override {}
        void scannerData(int, int, const ContractDetails&, const std::string&, const std::string&, const std::string&, const std::string&) override {}
        void scannerDataEnd(int) override {}
        void realtimeBar(TickerId, long, double, double, double, double, Decimal, Decimal, int) override {}
        void currentTime(long time) override;
        void fundamentalData(TickerId, const std::string&) override {}
        void deltaNeutralValidation(int, const DeltaNeutralContract&) override {}
        void tickSnapshotEnd(int) override {}
        void marketDataType(TickerId, int) override {}
        void commissionReport(const CommissionReport&) override {}
        void position(const std::string&, const Contract&, Decimal, double) override {}
        void positionEnd() override {}
        void accountSummary(int, const std::string&, const std::string&, const std::string&, const std::string&) override {}
        void accountSummaryEnd(int) override {}
        void verifyMessageAPI(const std::string&) override {}
        void verifyCompleted(bool, const std::string&) override {}
        void displayGroupList(int, const std::string&) override {}
        void displayGroupUpdated(int, const std::string&) override {}
        void verifyAndAuthMessageAPI(const std::string&, const std::string&) override {}
        void verifyAndAuthCompleted(bool, const std::string&) override {}
        void connectAck() override {}
        void positionMulti(int, const std::string&, const std::string&, const Contract&, Decimal, double) override {}
        void positionMultiEnd(int) override {}
        void accountUpdateMulti(int, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&) override {}
        void accountUpdateMultiEnd(int) override {}
        void securityDefinitionOptionalParameter(int, const std::string&, int, const std::string&, const std::string&, const std::set<std::string>&, const std::set<double>&) override {}
        void securityDefinitionOptionalParameterEnd(int) override {}
        void softDollarTiers(int, const std::vector<SoftDollarTier>&) override {}
        void familyCodes(const std::vector<FamilyCode>&) override {}
        void symbolSamples(int, const std::vector<ContractDescription>&) override {}
        void mktDepthExchanges(const std::vector<DepthMktDataDescription>&) override {}
        void tickNews(int, time_t, const std::string&, const std::string&, const std::string&, const std::string&) override {}
        void smartComponents(int, const SmartComponentsMap&) override {}
        void tickReqParams(int, double, const std::string&, int) override {}
        void newsProviders(const std::vector<NewsProvider>&) override {}
        void newsArticle(int, int, const std::string&) override {}
        void historicalNews(int, const std::string&, const std::string&, const std::string&, const std::string&) override {}
        void historicalNewsEnd(int, bool) override {}
        void headTimestamp(int, const std::string&) override {}
        void histogramData(int, const HistogramDataVector&) override {}
        void historicalDataUpdate(TickerId, const Bar&) override {}
        void rerouteMktDataReq(int, int, const std::string&) override {}
        void rerouteMktDepthReq(int, int, const std::string&) override {}
        void marketRule(int, const std::vector<PriceIncrement>&) override {}
        void pnl(int, double, double, double) override {}
        void pnlSingle(int, Decimal, double, double, double, double) override {}
        void historicalTicks(int, const std::vector<HistoricalTick>&, bool) override {}
        void historicalTicksBidAsk(int, const std::vector<HistoricalTickBidAsk>&, bool) override {}
        void historicalTicksLast(int, const std::vector<HistoricalTickLast>&, bool) override {}
        void tickByTickAllLast(int, int, time_t, double, Decimal, const TickAttribLast&, const std::string&, const std::string&) override {}
        void tickByTickBidAsk(int, time_t, double, double, Decimal, Decimal, const TickAttribBidAsk&) override {}
        void tickByTickMidPoint(int, time_t, double) override {}
        void orderBound(long long, int, int) override {}
        void completedOrder(const Contract&, const Order&, const OrderState&) override {}
        void completedOrdersEnd() override {}
        void replaceFAEnd(int, const std::string&) override {}
        void wshMetaData(int, const std::string&) override {}
        void wshEventData(int, const std::string&) override {}
        void historicalSchedule(int, const std::string&, const std::string&, const std::string&, const std::vector<HistoricalSession>&) override {}
        void userInfo(int, const std::string&) override {}
        // Override end




    private:
        int connect() override { return 1;};
        int connected(const char* host, int port, int clientId);
        int disconnect() override;
        double getLatestPrice(std::string ticker) override;
        int placeOrder(oms::Order order) override;
        oms::Position getLatestPosition(std::string ticker) override;

		void getCurrentTime();
        std::string ConvertStateToString();

        // Socket data
        EReaderOSSignal m_osSignal;
        EClientSocket * const m_pClient;
        time_t m_sleepDeadline;
        std::unique_ptr<EReader> m_pReader;
        State m_state;

        OrderId m_orderId;
        bool m_extraAuth;
        std::string m_bboExchange;

        // Price data
        std::unordered_map<TickerId, double> m_lastPrices;
        std::unordered_map<std::string, TickerId> m_tickerToId;
        std::mutex m_priceMutex;
        TickerId m_nextTickerId = 1001; // Arbitrary starting ID

        std::condition_variable m_priceUpdated;


};
