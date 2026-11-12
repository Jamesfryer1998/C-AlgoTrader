#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <ctime>
#include <string>
#include <nlohmann/json.hpp>
#include "PersistentConnection.hpp"

using json = nlohmann::json;

/**
 * AlpacaClient
 *
 * Tiny HTTPS helper shared by AlpacaStream and AlpacaBroker.
 * It adds the Alpaca API key headers to every request and turns the reply into JSON.
 * It keeps one connection open to each Alpaca host (PersistentConnection), so a request doesn't pay the
 * TCP and TLS handshakes every time: one at a time per client, from any thread.
 *
 * Keys are read from the environment variables APCA_API_KEY_ID and APCA_API_SECRET_KEY.
 * main() loads these from the git-ignored .env file (see .env.example).
 *
 * Every Alpaca class takes one in its constructor (a real one if none is given). Tests pass a fake that
 * overrides get / post / del / wait with canned replies, so they never touch the network or sleep.
 */
struct HttpResult
{
    int status = 0;         // HTTP status code (0 = could not connect)
    json body;              // Parsed JSON body (null if the body was not JSON)
    std::string error;      // Network error or Alpaca's error message
    double totalMs = 0;     // the whole request
    double connectMs = 0;   // of which opening a connection (0 when an open one was reused)

    bool ok() const { return status >= 200 && status < 300; }

    // Rate limited (429), no connection (0) or a server error: worth asking again shortly
    bool isBusy() const { return status == 429 || status == 0 || status >= 500; }
};

class AlpacaClient
{
    public:
        AlpacaClient();
        virtual ~AlpacaClient() = default;

        bool hasKeys() const { return !keyId.empty() && !secretKey.empty(); }
        const std::string& getKeyId() const { return keyId; }
        const std::string& getSecretKey() const { return secretKey; }

        virtual HttpResult get(const std::string& url);

        // GET, asking again while Alpaca is busy and waiting a little longer each time
        // (firstWait, then 2 x firstWait, ...). Gives up after maxAttempts requests.
        HttpResult getWhenFree(const std::string& url, int maxAttempts, std::chrono::seconds firstWait);
        virtual HttpResult post(const std::string& url, const json& body);
        virtual HttpResult del(const std::string& url);   // HTTP DELETE, e.g. cancel an order

        // Pause between retries
        virtual void wait(std::chrono::milliseconds duration);

        // Keep the connection to `url`'s host open: a cheap GET when it has been idle a while (the trader calls
        // this between orders, so an order never pays for a connection the server closed meanwhile)
        virtual void keepWarm(const std::string& url);

    protected:
        // Keys given directly (for tests), not read from the environment
        AlpacaClient(const std::string& keyId, const std::string& secretKey);

    private:
        HttpResult send(const std::string& method, const std::string& url, const std::string& body);
        HttpResult toResult(const PersistentConnection::Response& response);
        PersistentConnection* connectionFor(const std::string& url, std::string& pathAndQuery);

        std::string keyId;
        std::string secretKey;
        std::mutex lock;                                                        // one request at a time
        std::map<std::string, std::unique_ptr<PersistentConnection>> connections;   // by "https://host:port"
        std::map<std::string, std::chrono::steady_clock::time_point> lastUsed;      // by the same
};
