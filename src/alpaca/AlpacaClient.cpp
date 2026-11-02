#include "AlpacaClient.hpp"

#include <cstdlib>

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
AlpacaClient::post(const std::string& url, const json& body)
{
    return toResult(http.post(url, body.dump(), makeArgs()));
}
