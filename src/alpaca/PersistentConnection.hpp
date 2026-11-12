#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <ixwebsocket/IXSocket.h>
#include <ixwebsocket/IXSocketTLSOptions.h>
#include <ixwebsocket/IXWebSocketHttpHeaders.h>

/**
 * PersistentConnection
 *
 * One HTTP(S) connection to one host, kept open between requests (HTTP/1.1 keep-alive). Opening a secure
 * connection to Alpaca's US servers takes three or four round trips of ~80 ms from the UK before a request
 * can even be sent; reusing the connection, every request after the first is a single round trip.
 *
 * Built on IXWebSocket's own sockets (the same TLS as the market data stream), so nothing new to install.
 * Not thread safe: AlpacaClient keeps one per host behind a lock.
 *
 * A server closes connections that sit idle. A request on a reused connection that gets no reply at all never
 * reached it, so it's sent again once on a new connection (as curl does). Connections idle for longer than
 * MAX_IDLE are not trusted and are reopened first.
 */
class PersistentConnection
{
    public:
        struct Response
        {
            int status = 0;            // HTTP status (0 = no reply)
            std::string body;
            std::string error;         // why there was no reply
            double connectMs = 0;      // opening the connection (TCP + TLS); 0 when an open one was reused
            double totalMs = 0;        // the whole request, connecting included
            bool reused = false;       // went over a connection that was already open
        };

        using Headers = std::map<std::string, std::string>;

        // e.g. "https://paper-api.alpaca.markets"; every request's URL must be on this host
        PersistentConnection(bool tls, std::string host, int port, std::chrono::seconds connectTimeout,
                             std::chrono::seconds transferTimeout);

        Response request(const std::string& method, const std::string& pathAndQuery, const Headers& headers,
                         const std::string& body);

        bool isOpen() const { return socket != nullptr; }
        void close();

        // A connection idle longer than this may have been closed by the server: open a new one instead
        static constexpr std::chrono::seconds MAX_IDLE{50};

    private:
        enum class Outcome { Replied, NothingBack, Failed };

        bool open(std::string& error, double& connectMs);
        Outcome exchange(const std::string& request, Response& response);
        bool readBody(const ix::WebSocketHttpHeaders& headers, std::string& body);
        std::string requestText(const std::string& method, const std::string& pathAndQuery, const Headers& headers,
                                const std::string& body) const;

        bool tls;
        std::string host;
        int port;
        std::chrono::seconds connectTimeout;
        std::chrono::seconds transferTimeout;
        ix::SocketTLSOptions tlsOptions;
        std::unique_ptr<ix::Socket> socket;
        std::chrono::steady_clock::time_point lastUsed;
        std::chrono::steady_clock::time_point deadline;   // of the request in progress
};
