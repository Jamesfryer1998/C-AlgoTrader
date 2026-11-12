#include "AlpacaClient.hpp"

#include <cstdlib>
#include <iostream>
#include <thread>
#include <ixwebsocket/IXUrlParser.h>

namespace
{
    // Keys come from these environment variables (main() loads them from .env)
    const char* const KEY_ID_VARIABLE = "APCA_API_KEY_ID";
    const char* const SECRET_KEY_VARIABLE = "APCA_API_SECRET_KEY";

    // ...and go in these headers on every request
    const std::string KEY_ID_HEADER = "APCA-API-KEY-ID";
    const std::string SECRET_KEY_HEADER = "APCA-API-SECRET-KEY";
    const std::string JSON_CONTENT_TYPE = "application/json";
    const std::string CONTENT_TYPE_HEADER = "Content-Type";

    const int CONNECT_TIMEOUT_SECONDS = 5;
    const int TRANSFER_TIMEOUT_SECONDS = 10;

    const std::string HTTPS = "https";
    const std::string GET = "GET";
    const std::string POST = "POST";
    const std::string DELETE = "DELETE";

    // A connection idle this long gets a cheap request, well inside the time servers close idle ones
    const std::chrono::seconds KEEP_WARM_AFTER{20};
}

static std::string readEnv(const char* name)
{
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

AlpacaClient::AlpacaClient()
: keyId(readEnv(KEY_ID_VARIABLE)),
  secretKey(readEnv(SECRET_KEY_VARIABLE))
{
}

AlpacaClient::AlpacaClient(const std::string& keyId, const std::string& secretKey)
: keyId(keyId),
  secretKey(secretKey)
{
}

void
AlpacaClient::wait(std::chrono::milliseconds duration)
{
    std::this_thread::sleep_for(duration);
}

// The connection to `url`'s host (opened on first use), and the part of the URL after the host
PersistentConnection*
AlpacaClient::connectionFor(const std::string& url, std::string& pathAndQuery)
{
    std::string protocol, host, path, query;
    int port = 0;
    if (!ix::UrlParser::parse(url, protocol, host, path, query, port))
        return nullptr;
    pathAndQuery = path;
    std::string key = protocol + "://" + host + ":" + std::to_string(port);
    auto& connection = connections[key];
    if (!connection)
        connection = std::make_unique<PersistentConnection>(protocol == HTTPS, host, port,
                                                            std::chrono::seconds(CONNECT_TIMEOUT_SECONDS),
                                                            std::chrono::seconds(TRANSFER_TIMEOUT_SECONDS));
    lastUsed[key] = std::chrono::steady_clock::now();
    return connection.get();
}

HttpResult
AlpacaClient::send(const std::string& method, const std::string& url, const std::string& body)
{
    std::lock_guard<std::mutex> guard(lock);
    std::string pathAndQuery;
    PersistentConnection* connection = connectionFor(url, pathAndQuery);
    if (!connection)
    {
        HttpResult bad;
        bad.error = "Not a URL: " + url;
        return bad;
    }
    PersistentConnection::Headers headers = {{KEY_ID_HEADER, keyId}, {SECRET_KEY_HEADER, secretKey},
                                             {CONTENT_TYPE_HEADER, JSON_CONTENT_TYPE}};
    return toResult(connection->request(method, pathAndQuery, headers, body));
}

HttpResult
AlpacaClient::toResult(const PersistentConnection::Response& response)
{
    HttpResult result;
    result.status = response.status;
    result.totalMs = response.totalMs;
    result.connectMs = response.connectMs;
    result.body = json::parse(response.body, nullptr, false); // false = don't throw on bad JSON

    if (result.body.is_discarded())
        result.body = nullptr;

    if (!response.error.empty())
        result.error = response.error;
    else if (!result.ok() && result.body.is_object() && result.body.contains("message"))
        result.error = result.body["message"].get<std::string>();
    else if (!result.ok())
        result.error = response.body;

    return result;
}

HttpResult
AlpacaClient::get(const std::string& url)
{
    return send(GET, url, "");
}

HttpResult
AlpacaClient::getWhenFree(const std::string& url, int maxAttempts, std::chrono::seconds firstWait)
{
    HttpResult result;
    for (int attempt = 1; attempt <= maxAttempts; attempt++)
    {
        result = get(url);
        if (!result.isBusy() || attempt == maxAttempts)
            break;
        std::chrono::seconds pause = firstWait * attempt;
        std::cerr << "  Alpaca busy (" << result.status << "), retrying in " << pause.count() << "s..." << std::endl;
        wait(pause);
    }
    return result;
}

HttpResult
AlpacaClient::post(const std::string& url, const json& body)
{
    return send(POST, url, body.dump());
}

HttpResult
AlpacaClient::del(const std::string& url)
{
    return send(DELETE, url, "");
}

void
AlpacaClient::keepWarm(const std::string& url)
{
    std::string protocol, host, path, query;
    int port = 0;
    if (!ix::UrlParser::parse(url, protocol, host, path, query, port))
        return;
    std::string key = protocol + "://" + host + ":" + std::to_string(port);
    {
        std::lock_guard<std::mutex> guard(lock);
        auto used = lastUsed.find(key);
        if (used != lastUsed.end() && std::chrono::steady_clock::now() - used->second < KEEP_WARM_AFTER)
            return;
    }
    get(url);
}
