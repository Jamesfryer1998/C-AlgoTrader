#include "PersistentConnection.hpp"
#include <sstream>
#include <ixwebsocket/IXHttp.h>
#include <ixwebsocket/IXSocketFactory.h>

namespace
{
    const std::string HTTP_VERSION = "HTTP/1.1";
    const std::string LINE_END = "\r\n";
    const std::string CONTENT_LENGTH = "Content-Length";
    const std::string TRANSFER_ENCODING = "Transfer-Encoding";
    const std::string CHUNKED = "chunked";
    const std::string CONNECTION = "Connection";
    const std::string CLOSE = "close";
    const std::string KEEP_ALIVE = "keep-alive";
    const int HEX = 16;
    const int NO_FILE_DESCRIPTOR = -1;   // createSocket: open a new socket

    double millisecondsSince(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    std::string withoutLineEnd(std::string line)
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        return line;
    }

    bool contains(const std::string& text, const std::string& word)
    {
        std::string lower = text;
        for (auto& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return lower.find(word) != std::string::npos;
    }
}

PersistentConnection::PersistentConnection(bool tls, std::string host, int port, std::chrono::seconds connectTimeout,
                                           std::chrono::seconds transferTimeout)
: tls(tls),
  host(std::move(host)),
  port(port),
  connectTimeout(connectTimeout),
  transferTimeout(transferTimeout)
{
}

void
PersistentConnection::close()
{
    if (socket)
        socket->close();
    socket.reset();
}

PersistentConnection::Response
PersistentConnection::request(const std::string& method, const std::string& pathAndQuery, const Headers& headers,
                              const std::string& body)
{
    Response response;
    auto started = std::chrono::steady_clock::now();
    std::string text = requestText(method, pathAndQuery, headers, body);

    if (socket && started - lastUsed > MAX_IDLE)
        close();   // the server has probably closed it already: don't find out by losing a request

    // A reused connection that sends nothing back was closed by the server before the request reached it:
    // the request wasn't seen, so it's safe to send once more on a new connection
    for (int attempt = 0; attempt < 2; attempt++)
    {
        response.reused = socket != nullptr;
        if (!socket && !open(response.error, response.connectMs))
            break;
        Outcome outcome = exchange(text, response);
        if (outcome == Outcome::Replied)
            break;
        close();
        if (outcome == Outcome::Failed || !response.reused)
            break;
    }
    response.totalMs = millisecondsSince(started);
    lastUsed = std::chrono::steady_clock::now();
    return response;
}

bool
PersistentConnection::open(std::string& error, double& connectMs)
{
    auto started = std::chrono::steady_clock::now();
    socket = ix::createSocket(tls, NO_FILE_DESCRIPTOR, error, tlsOptions);
    if (!socket)
        return false;
    auto connectDeadline = started + connectTimeout;
    auto timedOut = [connectDeadline] { return std::chrono::steady_clock::now() > connectDeadline; };
    if (!socket->connect(host, port, error, timedOut))
    {
        socket.reset();
        error = "Can't connect to " + host + ": " + error;
        return false;
    }
    connectMs = millisecondsSince(started);
    return true;
}

// Send the request and read the whole reply. NothingBack: not a byte came back (the connection was dead)
PersistentConnection::Outcome
PersistentConnection::exchange(const std::string& request, Response& response)
{
    deadline = std::chrono::steady_clock::now() + transferTimeout;
    auto timedOut = [this] { return std::chrono::steady_clock::now() > deadline; };

    if (!socket->writeBytes(request, timedOut))
    {
        response.error = "Couldn't send the request to " + host;
        return Outcome::NothingBack;
    }
    auto [gotStatus, statusLine] = socket->readLine(timedOut);
    if (!gotStatus || statusLine.empty())
    {
        response.error = "No reply from " + host;
        return Outcome::NothingBack;
    }
    response.status = ix::Http::parseStatusLine(withoutLineEnd(statusLine)).second;

    auto [gotHeaders, headers] = ix::parseHttpHeaders(socket, timedOut);
    if (!gotHeaders || !readBody(headers, response.body))
    {
        response.error = "The reply from " + host + " was cut short";
        return Outcome::Failed;
    }
    auto connection = headers.find(CONNECTION);
    if (connection != headers.end() && contains(connection->second, CLOSE))
        close();   // the server is closing it: the next request opens a new one
    return Outcome::Replied;
}

bool
PersistentConnection::readBody(const ix::WebSocketHttpHeaders& headers, std::string& body)
{
    auto timedOut = [this] { return std::chrono::steady_clock::now() > deadline; };
    auto length = headers.find(CONTENT_LENGTH);
    if (length != headers.end())
    {
        size_t size = std::stoul(length->second);
        if (size == 0)
            return true;
        auto [ok, bytes] = socket->readBytes(size, nullptr, nullptr, timedOut);
        body = std::move(bytes);
        return ok;
    }
    auto encoding = headers.find(TRANSFER_ENCODING);
    if (encoding != headers.end() && contains(encoding->second, CHUNKED))
    {
        while (true)
        {
            auto [ok, sizeLine] = socket->readLine(timedOut);
            if (!ok)
                return false;
            size_t size = std::stoul(withoutLineEnd(sizeLine), nullptr, HEX);
            if (size == 0)
                return socket->readLine(timedOut).first;   // the blank line after the last chunk
            auto [gotChunk, chunk] = socket->readBytes(size, nullptr, nullptr, timedOut);
            if (!gotChunk || !socket->readLine(timedOut).first)   // each chunk ends with a line end
                return false;
            body += chunk;
        }
    }
    return true;   // no body (e.g. 204 No Content)
}

std::string
PersistentConnection::requestText(const std::string& method, const std::string& pathAndQuery, const Headers& headers,
                                  const std::string& body) const
{
    std::ostringstream text;
    text << method << " " << pathAndQuery << " " << HTTP_VERSION << LINE_END
         << "Host: " << host << LINE_END
         << CONNECTION << ": " << KEEP_ALIVE << LINE_END
         << "Accept: */*" << LINE_END;
    for (const auto& [name, value] : headers)
        text << name << ": " << value << LINE_END;
    if (!body.empty() || method == "POST" || method == "PATCH")
        text << CONTENT_LENGTH << ": " << body.size() << LINE_END;
    text << LINE_END << body;
    return text.str();
}
