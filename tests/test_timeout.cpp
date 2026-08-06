#include "../network/udp_socket.h"
#include "../common/logger.h"

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib,"ws2_32.lib")
#endif

int main()
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2),&wsa);
#endif

    UDPSocket udp;

    if(!udp.create())
        return 1;

    udp.bind(4000);

    udp.setReceiveTimeout(1000);

    RDTPacket packet;

    std::string ip;

    unsigned short port;

    log_info("Waiting packet...");

    if(!udp.receivePacket(packet,ip,port))
    {
        log_info("Timeout OK");
    }
    else
    {
        log_error("Unexpected packet");
    }

#ifdef _WIN32
    WSACleanup();
#endif
}