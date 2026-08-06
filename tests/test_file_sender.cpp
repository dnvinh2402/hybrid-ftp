#include "../network/file_sender.h"
#include "../network/rdt_sender.h"
#include "../network/udp_socket.h"

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
        std::cout << "Usage:\n";
        std::cout << "test_file_sender server\n";
        std::cout << "test_file_sender client\n";
        return 0;
    }

    UDPSocket socket;

    if (!socket.create())
        return 1;

    std::string mode = argv[1];

    if (mode == "server")
    {
        socket.bind(5000);
        socket.setReceiveTimeout(1000);

        RDTPacket packet;

        std::string ip;
        unsigned short port;

        std::cout << "Waiting packets...\n\n";

        while (true)
        {
            if (!socket.receivePacket(packet, ip, port))
                continue;

            bool isFin = false;

            if (packet.header.flags & RDTFlag::META)
            {
                std::cout << "[META]\n";
                std::cout << "SEQ : "
                          << packet.header.seq_num
                          << std::endl;

                FileMetadata meta;

                std::memcpy(
                    &meta,
                    packet.payload,
                    sizeof(FileMetadata));

                std::cout << "File : "
                          << meta.fileName
                          << std::endl;

                std::cout << "Size : "
                          << meta.fileSize
                          << std::endl;
            }

            else if (packet.header.flags & RDTFlag::DATA)
            {
                std::cout << "[DATA]\n";

                std::cout
                    << "SEQ : "
                    << packet.header.seq_num
                    << std::endl;

                std::cout
                    << "Payload : "
                    << packet.header.payload_len
                    << " bytes"
                    << std::endl;
            }

            else if (packet.header.flags & RDTFlag::FIN)
            {
                std::cout << "[FIN]\n";

                std::cout
                    << "SEQ : "
                    << packet.header.seq_num
                    << std::endl;

                isFin = true;
            }
            auto ack =
                PacketBuilder::buildAckPacket(
                    packet.header.seq_num);

            socket.sendPacket(
                ack,
                ip,
                port);
            if (isFin)
                break;
        }
    }
    else
    {
        socket.bind(4000);
        socket.setReceiveTimeout(1000);

        RDTSender sender(socket);

        FileSender fileSender(sender);

        if (!fileSender.sendFile(
                "sample.txt",
                "127.0.0.1",
                5000))
        {
            std::cout
                << "Send failed\n";

            return 1;
        }

        const auto &session =
            fileSender.getSession();

        std::cout << "\n===== Session =====\n";

        std::cout
            << "File : "
            << session.fileName
            << std::endl;

        std::cout
            << "File Size : "
            << session.fileSize
            << std::endl;

        std::cout
            << "Packets : "
            << session.packetsTransferred
            << std::endl;

        std::cout
            << "Bytes : "
            << session.bytesTransferred
            << std::endl;

        std::cout
            << "Finished : "
            << session.finished
            << std::endl;
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}