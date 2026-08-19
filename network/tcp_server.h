#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "../common/socket_platform.h"

#include <cstdint>

class TCPServer
{
public:
    explicit TCPServer(
        std::uint16_t port = 2121);

    ~TCPServer();

    bool start();

    void stop();

    SOCKET acceptClient();

    bool isRunning() const;

private:
    std::uint16_t port;

    bool running;

    SOCKET listenSocket;
};

#endif