#ifndef UDP_SOCKET_H
#define UDP_SOCKET_H

#include <string>
#include "../common/rdt_packet.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

typedef int SOCKET;

#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

class UDPSocket 
{
private:
    SOCKET socketFd;

public:
    UDPSocket();

    ~UDPSocket();

    bool create();

    bool bind(unsigned short port);

    bool sendPacket(const RDTPacket &packet,
                    const std::string &ip,
                    unsigned short port);

    bool receivePacket(RDTPacket &packet,
                       std::string &senderIp,
                       unsigned short &senderPort);

    void close();
};

#endif