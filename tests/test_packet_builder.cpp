#pragma
#include <iostream>
#include <cstring>
#include <cstdint>

#include "../network/packet_builder.h"

int main()
{
    auto dataPacket =
        PacketBuilder::buildDataPacket(
            10,
            "Hello",
            5);

    std::cout << "===== DATA PACKET =====\n";

    std::cout << "SEQ: "
              << dataPacket.header.seq_num
              << '\n';

    std::cout << "ACK: "
              << dataPacket.header.ack_num
              << '\n';

    std::cout << "FLAGS: "
              << (int)dataPacket.header.flags
              << '\n';

    std::cout << "LEN: "
              << dataPacket.header.payload_len
              << '\n';

    std::cout << "CHECKSUM: "
              << dataPacket.header.checksum
              << '\n';

    std::cout << "PAYLOAD: ";

    std::cout.write(
        dataPacket.payload,
        dataPacket.header.payload_len);

    std::cout << "\n\n";
    auto ackPacket =
        PacketBuilder::buildAckPacket(10);

    std::cout
        << "===== ACK PACKET =====\n";

    std::cout
        << "ACK NUMBER: "
        << ackPacket.header.ack_num
        << '\n';

    std::cout
        << "FLAGS: "
        << (int)ackPacket.header.flags
        << '\n';

    std::cout
        << "CHECKSUM: "
        << ackPacket.header.checksum
        << "\n\n";
    auto finPacket =
        PacketBuilder::buildFinPacket(5);

    std::cout
        << "===== FIN PACKET =====\n";

    std::cout
        << "FLAGS: "
        << (int)finPacket.header.flags
        << '\n';

    std::cout
        << "CHECKSUM: "
        << finPacket.header.checksum
        << '\n';
    std::cout << "\n";

    if (verifyChecksum(dataPacket))
        std::cout << "DATA checksum OK\n";
    else
        std::cout << "DATA checksum FAILED\n";

    if (verifyChecksum(ackPacket))
        std::cout << "ACK checksum OK\n";
    else
        std::cout << "ACK checksum FAILED\n";

    if (verifyChecksum(finPacket))
        std::cout << "FIN checksum OK\n";
    else
        std::cout << "FIN checksum FAILED\n";

    return 0;
}