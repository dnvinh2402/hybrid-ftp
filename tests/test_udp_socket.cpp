#include "../network/udp_socket.h"
#include "../common/logger.h"

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

int main()
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    UDPSocket socket;

    if (!socket.create())
        return 1;
    if (!socket.bind(3000))
        return 1;

    log_info("Test Passed.");


#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}