#include <cassert>
#include <iostream>

#include "../common/socket_platform.h"
#include "../network/udp_socket.h"

int main()
{
    assert(SocketPlatform::initialize());

    UDPSocket socket;

    assert(socket.create());

    assert(socket.bind(0));

    assert(
        socket.setReceiveTimeout(100));

    socket.close();

    SocketPlatform::cleanup();

#ifdef _WIN32

    std::cout
        << "socket_compat_tests passed "
           "on Windows/Winsock\n";

#else

    std::cout
        << "socket_compat_tests passed "
           "on POSIX sockets\n";

#endif

    return 0;
}