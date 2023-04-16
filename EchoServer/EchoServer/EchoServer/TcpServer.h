#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <cstdint>
#include <cstdlib>
#include <string>
#include <memory>

namespace TcpServer {

struct SocketConfig
{
    const uint16_t port;
};

struct ServerConfig
{
    const size_t bufferSize;
    const unsigned int maxConnectionsCount;
    const int delaySec;
};

class IAnswerStrategy
{
public:
    virtual ~IAnswerStrategy() = default;
    virtual void getAnswer(const char *in, char *out, size_t outSize) const = 0;
};

class ILogger
{
public:
    virtual ~ILogger() = default;
    virtual void info(const std::string &text) const = 0;
    virtual void error(const std::string &text) const = 0;
};

class Server
{
public:
    explicit Server(const SocketConfig &socketConfig, const ServerConfig &serverConfig, const IAnswerStrategy &answerStrategy, const TcpServer::ILogger &logger);
    ~Server();

    void run();
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}


#endif // TCPSERVER_H
