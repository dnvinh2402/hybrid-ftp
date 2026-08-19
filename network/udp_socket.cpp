#include "udp_socket.h"

#include "../common/logger.h"
#include "../common/socket_platform.h"

#include <array>
#include <cstring>
#include <string>

UDPSocket::UDPSocket()
{
    socketFd = INVALID_SOCKET;
}

UDPSocket::~UDPSocket()
{
    close();
}

bool UDPSocket::create()
{
    socketFd =
        socket(
            AF_INET,
            SOCK_DGRAM,
            0);

    if (socketFd == INVALID_SOCKET)
    {
        log_error(
            "Failed to create UDP socket. "
            "OS error="
            + std::to_string(
                SocketPlatform::lastError()));

        return false;
    }

    log_info(
        "UDP socket created successfully.");

    return true;
}

void UDPSocket::close()
{
    if (socketFd != INVALID_SOCKET)
    {
        SocketPlatform::close(
            socketFd);

        socketFd =
            INVALID_SOCKET;

        log_info(
            "UDP socket closed.");
    }
}

bool UDPSocket::bind(unsigned short port)
{
    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (::bind(
            socketFd,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address))
        == SOCKET_ERROR)
    {
        log_error(
            "Failed to bind UDP socket. "
            "OS error="
            + std::to_string(
                SocketPlatform::lastError()));

        return false;
    }

    log_info("UDP socket bound to port "
             + std::to_string(port));

    return true;
}
bool UDPSocket::receivePacket(RDTPacket& packet,
                              std::string& senderIp,
                              unsigned short& senderPort)
{
    sockaddr_in senderAddr{};
    SocketPlatform::Length addrLen = sizeof(senderAddr);

    uint8_t buffer[RDT_HEADER_SIZE + MAX_PAYLOAD_SIZE];

    int received = recvfrom(
        socketFd,
        reinterpret_cast<char*>(buffer),
        sizeof(buffer),
        0,
        reinterpret_cast<sockaddr*>(&senderAddr),
        &addrLen
    );

    if (received == SOCKET_ERROR)
    {
        const int errorCode = SocketPlatform::lastError();

        if (!SocketPlatform::isTimeoutError(errorCode))
        {
            log_error(
                "Failed to receive UDP packet. OS error="
                + std::to_string(errorCode));
        }

        return false;
    }

    if (!unpackFromRecv(buffer, received, packet))
    {
        log_error("Received invalid packet.");
        return false;
    }

    char ipBuffer[INET_ADDRSTRLEN];

    inet_ntop(
        AF_INET,
        &senderAddr.sin_addr,
        ipBuffer,
        sizeof(ipBuffer)
    );

    senderIp = ipBuffer;
    senderPort = ntohs(senderAddr.sin_port);

    return true;
}
bool UDPSocket::sendPacket(const RDTPacket& packet,
                           const std::string& ip,
                           unsigned short port)
{
    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    std::string cleanIp = ip;
    cleanIp.erase(cleanIp.find_last_not_of(" \n\r\t") + 1);
    cleanIp.erase(0, cleanIp.find_first_not_of(" \n\r\t"));

    if (inet_pton(AF_INET, cleanIp.c_str(), &address.sin_addr) <= 0)
    {
        log_error("Invalid IPv4 address: '" + cleanIp + "'.");
        return false;
    }


    std::array<uint8_t,
               RDT_HEADER_SIZE + MAX_PAYLOAD_SIZE> buffer;

    size_t packetSize =
        packForSend(packet,
                    buffer.data(),
                    buffer.size());

    int sent =
        sendto(socketFd,
               reinterpret_cast<const char*>(buffer.data()),
               static_cast<int>(packetSize),
               0,
               reinterpret_cast<sockaddr*>(&address),
               sizeof(address));

    if(sent == SOCKET_ERROR)
    {
        int errCode = SocketPlatform::lastError();
        log_error("Failed to send UDP packet. OS error=" + std::to_string(errCode));
        
        return false;
    }

    return true;
}
bool UDPSocket::setReceiveTimeout(int milliseconds)
{
#ifdef _WIN32

    DWORD timeout = milliseconds;

    if (setsockopt(
            socketFd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout),
            sizeof(timeout))
        == SOCKET_ERROR)
    {
        log_error("Failed to set receive timeout.");
        return false;
    }

#else

    timeval tv;
    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;

    if (setsockopt(
            socketFd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &tv,
            sizeof(tv))
        == SOCKET_ERROR)
    {
        log_error("Failed to set receive timeout.");
        return false;
    }

#endif

    return true;
}