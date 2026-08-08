#include "../network/udp_socket.h"
#include "../network/rdt_sender.h"
#include "../network/rdt_receiver.h"
#include "../network/packet_builder.h"

#include <iostream>

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
        std::cout
            << "Usage:\n"
            << "test_retransmission server\n"
            << "test_retransmission client\n";
        return 0;
    }

    UDPSocket udp;

    udp.create();

    std::string mode = argv[1];

    if (mode == "server")
    {
        udp.bind(5001);

        RDTReceiver receiver(udp);

        RDTPacket pkt;

        std::string ip;
        unsigned short port;

        int receiveCount = 0;

        while (true)
        {
            if (!receiver.receive(pkt, ip, port))
            {
                std::cout << "Receive failed." << std::endl;
                continue;
            }

            receiveCount++;

            std::cout << "=============================\n";
            std::cout << "Packet #" << receiveCount << std::endl;
            std::cout << "Sender : " << ip << ":" << port << std::endl;
            std::cout << "SEQ    : " << pkt.header.seq_num << std::endl;
            std::cout << "LEN    : " << pkt.header.payload_len << std::endl;
            std::cout << "FLAGS  : " << (int)pkt.header.flags << std::endl;
            std::cout << "DATA   : ";
            std::cout.write(pkt.payload, pkt.header.payload_len);
            std::cout << std::endl;

            // Chỉ test 2 packet rồi thoát
            if (receiveCount == 2)
            {
                std::cout << "\nRetransmission Test Finished\n";
                break;
            }
        }
    }
    else
    {
        udp.setReceiveTimeout(1000);

        RDTSender sender(udp);

        auto pkt =
            PacketBuilder::buildDataPacket(
                1,
                "Hello",
                5);

        sender.send(
            pkt,
            "127.0.0.1",
            5001);
    }

#ifdef _WIN32
    WSACleanup();
#endif
}