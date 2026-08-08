#include "packet_parser.h"

#include <cstring>

bool PacketParser::isMeta(
    const RDTPacket& packet)
{
    return packet.header.flags & RDTFlag::META;
}

bool PacketParser::isData(
    const RDTPacket& packet)
{
    return packet.header.flags & RDTFlag::DATA;
}

bool PacketParser::isAck(
    const RDTPacket& packet)
{
    return packet.header.flags & RDTFlag::ACK;
}

bool PacketParser::isFin(
    const RDTPacket& packet)
{
    return packet.header.flags & RDTFlag::FIN;
}

bool PacketParser::parseMetadata(
    const RDTPacket& packet,
    FileMetadata& metadata)
{
    if(!isMeta(packet))
        return false;

    if(packet.header.payload_len != sizeof(FileMetadata))
        return false;

    std::memcpy(
        &metadata,
        packet.payload,
        sizeof(FileMetadata));

    return true;
}