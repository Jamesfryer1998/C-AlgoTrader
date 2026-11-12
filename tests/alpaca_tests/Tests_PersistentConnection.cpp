#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <atomic>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include "alpaca/AlpacaClient.hpp"
#include "alpaca/PersistentConnection.hpp"

namespace
{
    const std::string LOCALHOST = "127.0.0.1";
    const std::chrono::seconds TIMEOUT{2};
    const std::string OK_JSON = "{\"ok\":true}";

    /**
     * A tiny HTTP/1.1 server on a free local port, answering each request with `reply(request)`, keeping the
     * connection open unless told to hang up after each reply. Counts the connections it accepted.
     */
    class LocalServer
    {
        public:
            using Reply = std::function<std::string(const std::string& request)>;

            LocalServer(Reply reply, bool hangUpAfterEachReply = false)
            : reply(std::move(reply)), hangUp(hangUpAfterEachReply)
            {
                listener = ::socket(AF_INET, SOCK_STREAM, 0);
                int yes = 1;
                ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                address.sin_port = 0;   // any free port
                ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address));
                socklen_t size = sizeof(address);
                ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size);
                port = ntohs(address.sin_port);
                ::listen(listener, 4);
                thread = std::thread([this] { serve(); });
            }

            ~LocalServer()
            {
                stopping = true;
                ::shutdown(listener, SHUT_RDWR);
                ::close(listener);
                thread.join();
            }

            int port = 0;
            std::atomic<int> connections{0};
            std::vector<std::string> requests;

        private:
            void serve()
            {
                while (!stopping)
                {
                    int client = ::accept(listener, nullptr, nullptr);
                    if (client < 0)
                        return;
                    connections++;
                    std::string pending;
                    char buffer[4096];
                    while (true)
                    {
                        ssize_t got = ::recv(client, buffer, sizeof(buffer), 0);
                        if (got <= 0)
                            break;
                        pending.append(buffer, static_cast<size_t>(got));
                        size_t end = pending.find("\r\n\r\n");
                        if (end == std::string::npos)
                            continue;
                        size_t length = 0;
                        size_t field = pending.find("Content-Length: ");
                        if (field != std::string::npos && field < end)
                            length = std::stoul(pending.substr(field + 16));
                        if (pending.size() < end + 4 + length)
                            continue;
                        std::string request = pending.substr(0, end + 4 + length);
                        pending.erase(0, request.size());
                        requests.push_back(request);
                        std::string response = reply(request);
                        ::send(client, response.data(), response.size(), 0);
                        if (hangUp)
                            break;
                    }
                    ::close(client);
                }
            }

            Reply reply;
            bool hangUp;
            int listener = -1;
            std::atomic<bool> stopping{false};
            std::thread thread;
    };

    std::string jsonReply(const std::string& body, const std::string& extraHeader = "")
    {
        return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size())
             + "\r\n" + extraHeader + "\r\n" + body;
    }

    PersistentConnection connectTo(const LocalServer& server)
    {
        return PersistentConnection(false, LOCALHOST, server.port, TIMEOUT, TIMEOUT);
    }
}

TEST(PersistentConnectionTests, ReusesOneConnectionForEveryRequest)
{
    LocalServer server([](const std::string&) { return jsonReply(OK_JSON); });
    PersistentConnection connection = connectTo(server);

    auto first = connection.request("GET", "/v2/clock", {}, "");
    auto second = connection.request("GET", "/v2/orders?status=open", {}, "");
    EXPECT_EQ(first.status, 200);
    EXPECT_EQ(first.body, OK_JSON);
    EXPECT_FALSE(first.reused);
    EXPECT_TRUE(second.reused);
    EXPECT_EQ(second.connectMs, 0);
    EXPECT_EQ(second.body, OK_JSON);
    EXPECT_EQ(server.connections, 1);
    EXPECT_NE(server.requests[1].find("GET /v2/orders?status=open HTTP/1.1"), std::string::npos);
}

TEST(PersistentConnectionTests, SendsTheBodyAndHeaders)
{
    LocalServer server([](const std::string&) { return jsonReply(OK_JSON); });
    PersistentConnection connection = connectTo(server);
    std::string order = "{\"symbol\":\"KO\",\"qty\":\"10\"}";
    auto response = connection.request("POST", "/v2/orders", {{"APCA-API-KEY-ID", "key"}}, order);
    EXPECT_EQ(response.status, 200);
    const std::string& sent = server.requests[0];
    EXPECT_NE(sent.find("Content-Length: " + std::to_string(order.size())), std::string::npos);
    EXPECT_NE(sent.find("APCA-API-KEY-ID: key"), std::string::npos);
    EXPECT_EQ(sent.substr(sent.size() - order.size()), order);
}

TEST(PersistentConnectionTests, ReadsAChunkedReply)
{
    LocalServer server([](const std::string&) {
        return std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                           "5\r\n{\"a\":\r\n6\r\n[1,2]}\r\n0\r\n\r\n");
    });
    PersistentConnection connection = connectTo(server);
    auto first = connection.request("GET", "/x", {}, "");
    auto second = connection.request("GET", "/x", {}, "");   // the chunked reply was read to its end
    EXPECT_EQ(first.body, "{\"a\":[1,2]}");
    EXPECT_EQ(second.body, "{\"a\":[1,2]}");
    EXPECT_EQ(server.connections, 1);
}

TEST(PersistentConnectionTests, ReconnectsWhenTheServerHungUp)
{
    // As a server closing an idle connection: the next request finds it dead and goes again on a new one
    LocalServer server([](const std::string&) { return jsonReply(OK_JSON); }, true);
    PersistentConnection connection = connectTo(server);
    auto first = connection.request("GET", "/x", {}, "");
    auto second = connection.request("DELETE", "/v2/orders/abc", {}, "");
    EXPECT_EQ(first.status, 200);
    EXPECT_EQ(second.status, 200);
    EXPECT_EQ(server.connections, 2);
    EXPECT_EQ(server.requests.size(), 2u);   // each request reached the server exactly once
}

TEST(PersistentConnectionTests, OpensANewConnectionWhenTheServerSaysClose)
{
    LocalServer server([](const std::string&) { return jsonReply(OK_JSON, "Connection: close\r\n"); });
    PersistentConnection connection = connectTo(server);
    connection.request("GET", "/x", {}, "");
    EXPECT_FALSE(connection.isOpen());
    auto second = connection.request("GET", "/x", {}, "");
    EXPECT_FALSE(second.reused);
    EXPECT_EQ(second.status, 200);
}

TEST(PersistentConnectionTests, NoServerIsAnErrorNotAHang)
{
    int freePort;
    {
        LocalServer briefly([](const std::string&) { return std::string(); });
        freePort = briefly.port;
    }
    PersistentConnection connection(false, LOCALHOST, freePort, TIMEOUT, TIMEOUT);
    auto response = connection.request("GET", "/x", {}, "");
    EXPECT_EQ(response.status, 0);
    EXPECT_FALSE(response.error.empty());
}

namespace
{
    // The real client, with keys given directly
    class KeyedClient : public AlpacaClient
    {
        public:
            KeyedClient() : AlpacaClient("key", "secret") {}
    };
}

TEST(PersistentConnectionTests, TheAlpacaClientReusesItsConnectionAndKeepsItWarm)
{
    LocalServer server([](const std::string&) { return jsonReply(OK_JSON); });
    std::string base = "http://" + LOCALHOST + ":" + std::to_string(server.port);
    KeyedClient client;

    client.keepWarm(base + "/v2/clock");                   // never used: opens it now, before any order
    client.keepWarm(base + "/v2/clock");                   // just used: nothing to do
    HttpResult order = client.post(base + "/v2/orders", {{"symbol", "KO"}});
    HttpResult cancel = client.del(base + "/v2/orders/stop-1");

    EXPECT_EQ(server.requests.size(), 3u);
    EXPECT_EQ(server.connections, 1);
    EXPECT_TRUE(order.ok());
    EXPECT_EQ(order.body["ok"], true);
    EXPECT_EQ(order.connectMs, 0);                         // the warm connection was already open
    EXPECT_NE(server.requests[1].find("APCA-API-KEY-ID: key"), std::string::npos);
    EXPECT_NE(server.requests[2].find("DELETE /v2/orders/stop-1"), std::string::npos);
    EXPECT_TRUE(cancel.ok());
}
