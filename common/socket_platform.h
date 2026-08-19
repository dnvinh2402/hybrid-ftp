#ifndef SOCKET_PLATFORM_H
#define SOCKET_PLATFORM_H

// Cross-platform wrapper for socket APIs.
// Windows uses Winsock.
// macOS/Linux use POSIX sockets.

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

        return WSAStartup(
                   MAKEWORD(2, 2),
                   &wsaData)
               == 0;
    }

    inline void cleanup()
    {
        WSACleanup();
    }

    inline int close(
        SOCKET socketFd)
    {
        return closesocket(
            socketFd);
    }

    inline int lastError()
    {
        return WSAGetLastError();
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
        // POSIX sockets do not need
        // global initialization.
        return true;
    }

    inline void cleanup()
    {
        // POSIX sockets do not need
        // global cleanup.
    }

    inline int close(
        SOCKET socketFd)
    {
        return ::close(
            socketFd);
    }

    inline int lastError()
    {
        return errno;
    }
}

#endif

#endif

#pragma once

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

#else

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using SOCKET = int;

constexpr SOCKET INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;

#endif

namespace SocketPlatform
{

inline int getLastError()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

}