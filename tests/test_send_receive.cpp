#include "../network/udp_socket.h"
#include "../common/logger.h"
#include "../common/rdt_packet.h"

#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    if (argc != 2)
    {
        std::cout << "Usage:\n";
        std::cout << "test_send_receive server\n";
        std::cout << "test_send_receive client\n";
        return 0;
    }

    UDPSocket udp;

    if (!udp.create())
        return 1;

    std::string mode = argv[1];
    if (mode == "server")
    {
        udp.bind(3000);

        RDTPacket pkt;

        std::string ip;
        unsigned short port;

        log_info("Waiting packet...");

        udp.receivePacket(pkt, ip, port);

        std::cout << "Sender : " << ip << ":" << port << std::endl;

        std::cout << "SEQ : " << pkt.header.seq_num << std::endl;

        std::cout << "Payload : ";

        std::cout.write(pkt.payload, pkt.header.payload_len);

        std::cout << std::endl;
    }
    else
    {
        RDTPacket pkt{};

        pkt.header.seq_num = 1;

        pkt.header.ack_num = 0;

        pkt.header.flags = RDTFlag::DATA;

        strcpy(pkt.payload, "Hello UDP");

        pkt.header.payload_len = strlen(pkt.payload);

        fillChecksum(pkt);

        udp.sendPacket(pkt, "127.0.0.1", 3000);
    }
#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}