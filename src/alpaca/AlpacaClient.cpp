#include "AlpacaClient.hpp"

#include <cstdlib>
#include <iostream>
#include <thread>

static std::string readEnv(const char* name)
{
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

AlpacaClient::AlpacaClient()
: keyId(readEnv("APCA_API_KEY_ID")),
  secretKey(readEnv("APCA_API_SECRET_KEY"))
{
}

ix::HttpRequestArgsPtr
AlpacaClient::makeArgs()
{
    ix::HttpRequestArgsPtr args = http.createRequest();
    args->extraHeaders["APCA-API-KEY-ID"] = keyId;
    args->extraHeaders["APCA-API-SECRET-KEY"] = secretKey;
    args->extraHeaders["Content-Type"] = "application/json";
    args->connectTimeout = 5;   // seconds
    args->transferTimeout = 10; // seconds
    return args;
}

HttpResult
AlpacaClient::toResult(const ix::HttpResponsePtr& response)
{
    HttpResult result;
    result.status = response->statusCode;
    result.body = json::parse(response->body, nullptr, false); // false = don't throw on bad JSON

    if (result.body.is_discarded())
        result.body = nullptr;

    if (!response->errorMsg.empty())
        result.error = response->errorMsg;
    else if (!result.ok() && result.body.is_object() && result.body.contains("message"))
        result.error = result.body["message"].get<std::string>();
    else if (!result.ok())
        result.error = response->body;

    return result;
}

HttpResult
AlpacaClient::get(const std::string& url)
{
    return toResult(http.get(url, makeArgs()));
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
        std::chrono::seconds wait = firstWait * attempt;
        std::cerr << "  Alpaca busy (" << result.status << "), retrying in " << wait.count() << "s..." << std::endl;
        std::this_thread::sleep_for(wait);
    }
    return result;
}

HttpResult
AlpacaClient::post(const std::string& url, const json& body)
{
    return toResult(http.post(url, body.dump(), makeArgs()));
}

HttpResult
AlpacaClient::del(const std::string& url)
{
    return toResult(http.Delete(url, makeArgs()));
}
