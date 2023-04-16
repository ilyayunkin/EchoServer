#include <iostream>
#include <cstring>
#include <exception>
#include <signal.h>

#include "TcpServer.h"

namespace {
class EchoAnswerStrategy : public TcpServer::IAnswerStrategy
{
public:
    void getAnswer(const char *in, char *out, size_t outSize) const override
    {
        strncpy(out, in, outSize);
        out[outSize - 1] = '\0';
    }
};

class Logger : public TcpServer::ILogger
{
public:
    void info(const std::string &text) const override
    {
        std::cout << text << std::endl;
    }
    void error(const std::string &text) const override
    {
        std::cerr << text << std::endl;
    }
};
}

int main()
{
    // Если клиент разрывает соединение, прилетает сигнал и крашит приложение.
    signal(SIGPIPE, SIG_IGN);

    const TcpServer::SocketConfig config{7050};
    const TcpServer::ServerConfig serverConfig{.bufferSize = 1024, .maxConnectionsCount = 4096, .delaySec = 5};
    const EchoAnswerStrategy answerStrategy;
    const Logger errorReporter;

    try{
        TcpServer::Server server(config, serverConfig, answerStrategy, errorReporter);
        server.run();
    }catch(const std::exception &e){
        errorReporter.error(e.what());
    }

    return 0;
}
