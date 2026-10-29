#pragma once

#include <string>
#include <ixwebsocket/IXHttpClient.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * AlpacaClient
 *
 * Tiny HTTPS helper shared by AlpacaStream and AlpacaBroker.
 * It adds the Alpaca API key headers to every request and turns the reply into JSON.
 *
 * Keys are read from the environment variables APCA_API_KEY_ID and APCA_API_SECRET_KEY.
 * main() loads these from the git-ignored .env file (see .env.example).
 */
struct HttpResult
{
    int status = 0;         // HTTP status code (0 = could not connect)
    json body;              // Parsed JSON body (null if the body was not JSON)
    std::string error;      // Network error or Alpaca's error message

    bool ok() const { return status >= 200 && status < 300; }
};

class AlpacaClient
{
    public:
        AlpacaClient();

        bool hasKeys() const { return !keyId.empty() && !secretKey.empty(); }
        const std::string& getKeyId() const { return keyId; }
        const std::string& getSecretKey() const { return secretKey; }

        HttpResult get(const std::string& url);
        HttpResult post(const std::string& url, const json& body);

    private:
        HttpResult toResult(const ix::HttpResponsePtr& response);
        ix::HttpRequestArgsPtr makeArgs();

        std::string keyId;
        std::string secretKey;
        ix::HttpClient http;
};
