#ifndef UDP_SOCKET_H
#define UDP_SOCKET_H

#include <string>

#include "../common/rdt_packet.h"
#include "../common/socket_platform.h"

class UDPSocket 
{
private:
    SOCKET socketFd = INVALID_SOCKET;

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
    bool setReceiveTimeout(int milliseconds);

    SOCKET getSocketFd() const { return socketFd; }
};

#endif