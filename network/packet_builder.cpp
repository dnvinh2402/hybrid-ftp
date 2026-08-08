#include "packet_builder.h"

#include <cstring>

RDTPacket PacketBuilder::buildDataPacket(
    uint32_t seq,
    const char *data,
    uint16_t length)
{
    RDTPacket pkt{};

    pkt.header.seq_num = seq;

    pkt.header.ack_num = 0;

    pkt.header.flags = RDTFlag::DATA;

    pkt.header.payload_len = length;
    pkt.header.version = RDT_VERSION;
    pkt.header.magic = RDT_MAGIC;

    memcpy(pkt.payload, data, length);

    fillChecksum(pkt);

    return pkt;
}
// ACK
RDTPacket PacketBuilder::buildAckPacket(uint32_t ack)
{
    RDTPacket pkt{};

    pkt.header.flags = RDTFlag::ACK;

    pkt.header.ack_num = ack;

    pkt.header.payload_len = 0;

    pkt.header.version = RDT_VERSION;

    pkt.header.magic = RDT_MAGIC;

    fillChecksum(pkt);

    return pkt;
}
// fin
RDTPacket PacketBuilder::buildFinPacket(uint32_t seq)
{
    RDTPacket pkt{};

    pkt.header.seq_num = seq;

    pkt.header.ack_num = 0;

    pkt.header.flags = RDTFlag::FIN;

    pkt.header.payload_len = 0;

    pkt.header.version = RDT_VERSION;

    pkt.header.magic = RDT_MAGIC;

    fillChecksum(pkt);

    return pkt;
}
// build meta packet
static_assert(
        sizeof(FileMetadata) <= MAX_PAYLOAD_SIZE,
        "Metadata too large.");

RDTPacket PacketBuilder::buildMetaPacket(uint32_t seq, const FileMetadata &meta)
{

    RDTPacket pkt{};

    pkt.header.version = RDT_VERSION;

    pkt.header.magic = RDT_MAGIC;

    pkt.header.seq_num = seq;

    pkt.header.ack_num = 0;

    pkt.header.flags = RDTFlag::META;

    std::memcpy(
        pkt.payload,
        &meta,
        sizeof(FileMetadata));

    pkt.header.payload_len =
        sizeof(FileMetadata);

    fillChecksum(pkt);
    return pkt;
}

RDTPacket PacketBuilder::buildSynPacket(uint32_t seq)
{
    RDTPacket pkt{};
    pkt.header.seq_num = seq;
    pkt.header.ack_num = 0;
    pkt.header.flags = RDTFlag::SYN;
    pkt.header.payload_len = 0;
    pkt.header.version = RDT_VERSION;
    pkt.header.magic = RDT_MAGIC;
    fillChecksum(pkt);
    return pkt;
}