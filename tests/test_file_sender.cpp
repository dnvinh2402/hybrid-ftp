#include "../network/file_sender.h"
#include "../network/rdt_sender.h"
#include "../network/udp_socket.h"

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

    udp.create();

    RDTSender sender(udp);

    FileSender fs(sender);

    fs.sendFile(
        "sample.txt",
        "127.0.0.1",
        3000);

#ifdef _WIN32
    WSACleanup();
#endif
}