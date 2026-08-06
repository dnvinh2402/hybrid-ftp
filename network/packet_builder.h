#pragma once
#include "../common/file_metadata.h"
#include "../common/rdt_packet.h"

#include <string>

class PacketBuilder
{
public:
    static RDTPacket buildDataPacket(
        uint32_t seq,
        const char *data,
        uint16_t length);

    static RDTPacket buildAckPacket(
        uint32_t ack);

    static RDTPacket buildFinPacket(uint32_t seq);

        static RDTPacket buildMetaPacket(
            uint32_t seq,
            const FileMetadata &meta);
};