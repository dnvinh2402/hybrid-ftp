#include "../network/packet_builder.h"
#include "../common/rdt_packet.h"

#include <iostream>

int main()
{
    std::cout << "========== CHECKSUM TEST ==========\n\n";

    //---------------------------------
    // TEST 1
    //---------------------------------

    auto packet =
        PacketBuilder::buildDataPacket(
            1,
            "Hello",
            5);

    std::cout << "Test 1 : Original Packet\n";

    if (verifyChecksum(packet))
        std::cout << "[PASS] Checksum valid.\n";
    else
        std::cout << "[FAIL] Checksum invalid.\n";

    //---------------------------------
    // TEST 2
    //---------------------------------

    packet.payload[0] = 'X';

    std::cout << "\nTest 2 : Corrupted Packet\n";

    if (!verifyChecksum(packet))
        std::cout << "[PASS] Corruption detected.\n";
    else
        std::cout << "[FAIL] Corruption NOT detected.\n";

    return 0;
}