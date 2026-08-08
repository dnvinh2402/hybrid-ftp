#pragma once

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

#include <cstdint>

class TCPServer
{
public:
    explicit TCPServer(uint16_t port = 2121);
    ~TCPServer();

    bool start();
    void stop();

    int acceptClient();

    bool isRunning() const;

private:
    uint16_t port;
    bool running;

#ifdef _WIN32
    SOCKET listenSocket;
#else
    int listenSocket;
#endif
};