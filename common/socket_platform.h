#ifndef SOCKET_PLATFORM_H
#define SOCKET_PLATFORM_H

// Small cross-platform wrapper around the native socket API.
// Windows uses Winsock; macOS/Linux use POSIX sockets.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

namespace SocketPlatform
{
    using Length = int;

    inline bool initialize()
    {
        WSADATA wsaData{};
        return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    }

    inline void cleanup()
    {
        WSACleanup();
    }

    inline int close(SOCKET socketFd)
    {
        return closesocket(socketFd);
    }

    inline int lastError()
    {
        return WSAGetLastError();
    }

    inline bool isTimeoutError(int errorCode)
    {
        return errorCode == WSAETIMEDOUT ||
               errorCode == WSAEWOULDBLOCK;
    }
}

#else

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using SOCKET = int;

constexpr SOCKET INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;

namespace SocketPlatform
{
    using Length = socklen_t;

    inline bool initialize()
    {
        return true;
    }

    inline void cleanup()
    {
    }

    inline int close(SOCKET socketFd)
    {
        return ::close(socketFd);
    }

    inline int lastError()
    {
        return errno;
    }

    inline bool isTimeoutError(int errorCode)
    {
        return errorCode == EAGAIN ||
               errorCode == EWOULDBLOCK;
    }
}

#endif

#endif