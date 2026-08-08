#ifndef PACKET_PARSER_H
#define PACKET_PARSER_H

#include "../common/file_metadata.h"
#include "../common/rdt_packet.h"

class PacketParser
{
public:

    static bool isMeta(
        const RDTPacket& packet);

    static bool isData(
        const RDTPacket& packet);

    static bool isAck(
        const RDTPacket& packet);

    static bool isFin(
        const RDTPacket& packet);

    static bool parseMetadata(
        const RDTPacket& packet,
        FileMetadata& metadata);
};

#endif