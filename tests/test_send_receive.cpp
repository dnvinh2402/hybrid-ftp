#include "../network/udp_socket.h"
#include "../common/logger.h"
#include "../common/rdt_packet.h"
#include "../network/packet_builder.h"
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
        udp.setReceiveTimeout(1000);

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
        auto pkt =
            PacketBuilder::buildDataPacket(
                0,
                "Hello",
                5);

        udp.sendPacket(pkt, "127.0.0.1", 3000);
    }
#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}