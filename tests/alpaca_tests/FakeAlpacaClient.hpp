#pragma once

#include <chrono>
#include <string>
#include <vector>
#include "../../src/alpaca/AlpacaClient.hpp"

/**
 * FakeAlpacaClient
 *
 * Stands in for Alpaca in tests: answers each request from canned replies and records every request it got,
 * so a test can check exactly what would have been sent. Waits between retries are recorded, not slept.
 *
 *   auto alpaca = std::make_shared<FakeAlpacaClient>();
 *   alpaca->reply("GET", "/v2/account", 200, {{"status", "ACTIVE"}, {"cash", "1000"}});
 *   alpaca->reply("POST", "/v2/orders", 200, {{"id", "a1"}, {"status", "accepted"}});
 *   AlpacaBroker broker(config, alpaca);
 *
 * A request is answered by the reply whose URL part it contains (the longest such part, so "/v2/orders/a1"
 * beats "/v2/orders"). Several replies for the same method and URL part are given in turn, the last one
 * then repeating. A request with no reply gets a 404.
 */
class FakeAlpacaClient : public AlpacaClient
{
    public:
        struct Request
        {
            std::string method;   // GET, POST or DELETE
            std::string url;
            json body;            // POST only
        };

        static constexpr int NOT_FOUND = 404;

        explicit FakeAlpacaClient(bool hasKeys = true)
            : AlpacaClient(hasKeys ? "PKTEST" : "", hasKeys ? "SECRET" : "") {}

        void reply(const std::string& method, const std::string& urlPart, int status, json body = json::object())
        {
            HttpResult result;
            result.status = status;
            result.body = body;
            if (!result.ok())
                result.error = body.is_object() && body.contains("message") ? body["message"].get<std::string>()
                                                                            : body.dump();
            for (auto& rule : rules)
                if (rule.method == method && rule.urlPart == urlPart)
                {
                    rule.replies.push_back(result);
                    return;
                }
            rules.push_back({method, urlPart, {result}, 0});
        }

        HttpResult get(const std::string& url) override { return answer("GET", url, json()); }
        HttpResult post(const std::string& url, const json& body) override { return answer("POST", url, body); }
        HttpResult del(const std::string& url) override { return answer("DELETE", url, json()); }
        void wait(std::chrono::milliseconds duration) override { waits.push_back(duration); }

        // The requests whose URL contains urlPart, in the order they were made
        std::vector<Request> sent(const std::string& method, const std::string& urlPart = "") const
        {
            std::vector<Request> matching;
            for (const auto& request : requests)
                if (request.method == method && request.url.find(urlPart) != std::string::npos)
                    matching.push_back(request);
            return matching;
        }

        std::vector<Request> requests;
        std::vector<std::chrono::milliseconds> waits;

    private:
        struct Rule
        {
            std::string method;
            std::string urlPart;
            std::vector<HttpResult> replies;
            size_t used;
        };
        std::vector<Rule> rules;

        HttpResult answer(const std::string& method, const std::string& url, const json& body)
        {
            requests.push_back({method, url, body});
            Rule* best = nullptr;
            for (auto& rule : rules)
                if (rule.method == method && url.find(rule.urlPart) != std::string::npos
                    && (!best || rule.urlPart.size() > best->urlPart.size()))
                    best = &rule;
            if (!best)
            {
                HttpResult missing;
                missing.status = NOT_FOUND;
                missing.error = "no fake reply for " + method + " " + url;
                return missing;
            }
            HttpResult result = best->replies[std::min(best->used, best->replies.size() - 1)];
            best->used++;
            return result;
        }
};
