#include "packet_builder.h"

#include <cstring>

RDTPacket PacketBuilder::buildDataPacket(
    uint32_t seq,
    const char* data,
    uint16_t length)
{
    RDTPacket pkt{};

    pkt.header.seq_num = seq;

    pkt.header.ack_num = 0;

    pkt.header.flags = RDTFlag::DATA;

    pkt.header.payload_len = length;

    memcpy(pkt.payload,data,length);

    fillChecksum(pkt);

    return pkt;
}
//ACK
RDTPacket PacketBuilder::buildAckPacket(uint32_t ack)
{
    RDTPacket pkt{};

    pkt.header.flags = RDTFlag::ACK;

    pkt.header.ack_num = ack;

    pkt.header.payload_len = 0;

    fillChecksum(pkt);

    return pkt;
}
//fin
RDTPacket PacketBuilder::buildFinPacket(uint32_t seq)
{
    RDTPacket packet{};

    packet.header.seq_num = seq;

    packet.header.ack_num = 0;

    packet.header.flags = RDTFlag::FIN;

    packet.header.payload_len = 0;

    fillChecksum(packet);

    return packet;
}