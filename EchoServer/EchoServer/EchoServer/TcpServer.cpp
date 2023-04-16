#include "TcpServer.h"

#include <cassert>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <liburing.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace TcpServer {
class Server::Impl
{
    enum class OperationType
    {
        Accept,
        Read,
        Timer,
        Write,
    };
    struct Connection;

public:
    Impl(const SocketConfig &socketConfig, const ServerConfig &serverConfig, const IAnswerStrategy &answerStrategy, const TcpServer::ILogger &errorReporter);
    ~Impl();

    void run();

private:
    int createListenSocket(uint16_t port);
    struct io_uring createUring(unsigned int maxConnectionsCount);

    void processCompletedTask(struct io_uring_cqe *completedEntry);

    void enqueueAccept();
    void enqueueRead(Connection &connection);
    void enqueueTimer(Connection &connection);
    void enqueueWrite(Connection &connection);
    void enqueueAnswer(Connection &connection);

    void removeConnection(Connection *connection);
    void throwException(const char *text);

private:
    const ServerConfig m_serverConfig;
    const IAnswerStrategy &m_answerStrategy;
    const TcpServer::ILogger &m_logger;

    const int m_listenSocket = 0;
    struct io_uring m_uring;
    std::vector<std::shared_ptr<Connection>> m_connectionsList;
};

Server::Server(const SocketConfig &socketConfig, const ServerConfig &serverConfig, const IAnswerStrategy &answerStrategy, const TcpServer::ILogger &logger)
    : m_impl(new Impl(socketConfig, serverConfig, answerStrategy, logger))
{}

Server::~Server()
{}

void Server::run()
{
    m_impl->run();
}

// TODO : оставить один буффер
// TODO : уменьшить размер структуры
struct Server::Impl::Connection
{
    Connection(size_t bufferSize)
        : bufferSize(bufferSize)
        , inBuffer(std::vector<char> (bufferSize, '\0'))
        , outBuffer(inBuffer)
    {
    }

    ~Connection()
    {
        ::close(socket);
    }
    // Буферы
    const size_t bufferSize;
    std::vector<char> inBuffer;
    std::vector<char> outBuffer;
    // Параметры сокета
    int socket = -1;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    // Поля для io_uring
    OperationType operationType;
    unsigned int byteCount = -1;
};

Server::Impl::Impl(const SocketConfig &socketConfig, const ServerConfig &serverConfig, const IAnswerStrategy &answerStrategy, const ILogger &errorReporter)
    : m_serverConfig(serverConfig)
    , m_answerStrategy(answerStrategy)
    , m_logger(errorReporter)
    , m_listenSocket(createListenSocket(socketConfig.port))
    , m_uring(createUring(serverConfig.maxConnectionsCount))
    , m_connectionsList()
{
}

Server::Impl::~Impl()
{
    if(m_listenSocket > 0)
        close(m_listenSocket);
}

void Server::Impl::run()
{
    enqueueAccept();
    while (true)
    {
        struct io_uring_cqe *completedEntry;
        if(int ret = io_uring_wait_cqe(&m_uring, &completedEntry); ret != 0)
            throwException("Completion queue error");

        processCompletedTask(completedEntry);
        io_uring_cqe_seen(&m_uring, completedEntry);
    }
}

int Server::Impl::createListenSocket(uint16_t port)
{
    const int listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == 0)
        throwException("socket failed");

    if (constexpr int opt = 1; setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
        throwException("setsockopt");

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( port );

    if (bind(listenSocket, (struct sockaddr *)&address, sizeof(address))<0)
        throwException("bind failed");

    if (listen(listenSocket, 3) < 0)
        throwException("listen");

    m_logger.info(std::string("Listening on port ") + std::to_string(port));
    return listenSocket;
}

io_uring Server::Impl::createUring(unsigned int maxConnectionsCount)
{
    struct io_uring uring;
    io_uring_queue_init(maxConnectionsCount, &uring, 0);
    return uring;
}

void Server::Impl::processCompletedTask(io_uring_cqe *completedEntry)
{
    auto *connection = (Connection *) completedEntry->user_data;
    switch (connection->operationType)
    {
    case OperationType::Accept:
    {
        connection->socket = completedEntry->res;
        if(connection->socket <= 0){
            m_logger.error("Cannot accept a new connection");
            enqueueAccept();
            break;
        }else{

        }
        m_logger.info(std::string("Connection accepted from ") + inet_ntoa(connection->address.sin_addr));
        enqueueRead(*connection);
        enqueueAccept();
        break;
    }
    case OperationType::Read:
    {
        auto bytesCount = completedEntry->res;
        if(bytesCount <= 0){
            removeConnection(connection);
            break;
        }

        m_logger.info(std::string("Received: ") + connection->inBuffer.data() + " from " + inet_ntoa(connection->address.sin_addr));
        enqueueTimer(*connection);
        break;
    }
    case OperationType::Timer:
    {
        if(completedEntry->res <= 0 && completedEntry->res != -ETIME){
            removeConnection(connection);
            break;
        }
        if(completedEntry->res != -ETIME)
            break;

        enqueueAnswer(*connection);
        break;
    }
    case OperationType::Write:
    {
        enqueueRead(*connection);
        break;
    }
    default:
        m_logger.error("Unknown operation");
        break;
    }
}

void Server::Impl::enqueueAccept()
{
    auto connection = std::make_shared<Connection>(m_serverConfig.bufferSize);
    m_connectionsList.push_back(connection);
    connection->operationType = OperationType::Accept;

    struct io_uring_sqe *taskEntry = io_uring_get_sqe(&m_uring);
    io_uring_prep_accept(taskEntry, m_listenSocket, (struct sockaddr *)&connection->address, &connection->addrlen, 0);
    io_uring_sqe_set_data(taskEntry, connection.get());
    io_uring_submit(&m_uring);
}

void Server::Impl::enqueueRead(Connection &connection)
{
    connection.operationType = OperationType::Read;
    connection.byteCount = connection.bufferSize;

    struct io_uring_sqe *taskEntry = io_uring_get_sqe(&m_uring);
    io_uring_prep_read(taskEntry, connection.socket, connection.inBuffer.data(), connection.bufferSize, /*offset*/0);
    io_uring_sqe_set_data(taskEntry, &connection);
    io_uring_submit(&m_uring);
}

void Server::Impl::enqueueTimer(Connection &connection)
{
    static struct __kernel_timespec timeout {.tv_sec = 5, .tv_nsec = 0};
    connection.operationType = OperationType::Timer;

    struct io_uring_sqe *taskEntry = io_uring_get_sqe(&m_uring);
    io_uring_prep_timeout(taskEntry, &timeout, 0, 0);
    io_uring_sqe_set_data(taskEntry, &connection);
    io_uring_submit(&m_uring);
}

void Server::Impl::enqueueWrite(Connection &connection)
{
    connection.operationType = OperationType::Write;

    struct io_uring_sqe *taskEntry = io_uring_get_sqe(&m_uring);
    io_uring_prep_write(taskEntry, connection.socket, connection.outBuffer.data(), connection.byteCount, 0);
    io_uring_sqe_set_data(taskEntry, &connection);
    io_uring_submit(&m_uring);
}

void Server::Impl::enqueueAnswer(Connection &connection)
{
    m_answerStrategy.getAnswer(connection.inBuffer.data(), connection.outBuffer.data(), connection.outBuffer.size());
    enqueueWrite(connection);
}

void Server::Impl::removeConnection(Connection *connection)
{
    m_logger.info("Connection closed");
    // TODO: удаление с O(1)
    m_connectionsList.erase(std::remove_if(m_connectionsList.begin(), m_connectionsList.end(), [connection](auto &sharedPtr){return sharedPtr.get() == connection;}), m_connectionsList.end());
}

void Server::Impl::throwException(const char *text)
{
    throw std::runtime_error(text);
}

}
