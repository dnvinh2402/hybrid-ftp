#include "packet_builder.h"

#include <cstring>

#include "../common/rdt_packet.h"

static_assert(
    sizeof(FileMetadata) <= MAX_PAYLOAD_SIZE,
    "Metadata too large.");

RDTPacket PacketBuilder::buildDataPacket(
    uint32_t seq,
    const char *data,
    uint16_t length)
{
    RDTPacket pkt{};

    if (data == nullptr)
    {
        return pkt;
    }

    if (length > MAX_PAYLOAD_SIZE)
    {
        return pkt;
    }

    pkt.header.version = RDT_VERSION;
    pkt.header.magic = RDT_MAGIC;

    pkt.header.seq_num = seq;
    pkt.header.ack_num = 0;

    pkt.header.flags = RDTFlag::DATA;

    pkt.header.payload_len = length;

    std::memcpy(
        pkt.payload,
        data,
        length);

    fillChecksum(pkt);

    return pkt;
}

RDTPacket PacketBuilder::buildAckPacket(
    uint32_t ack)
{
    RDTPacket pkt{};

    pkt.header.version = RDT_VERSION;
    pkt.header.magic = RDT_MAGIC;

    pkt.header.seq_num = 0;
    pkt.header.ack_num = ack;

    pkt.header.flags = RDTFlag::ACK;
    pkt.header.payload_len = 0;

    fillChecksum(pkt);

    return pkt;
}

RDTPacket PacketBuilder::buildFinPacket(
    uint32_t seq)
{
    RDTPacket pkt{};

    pkt.header.version = RDT_VERSION;
    pkt.header.magic = RDT_MAGIC;

    pkt.header.seq_num = seq;
    pkt.header.ack_num = 0;

    pkt.header.flags = RDTFlag::FIN;
    pkt.header.payload_len = 0;

    fillChecksum(pkt);

    return pkt;
}

RDTPacket PacketBuilder::buildMetaPacket(
    uint32_t seq,
    const FileMetadata &meta)
{
    RDTPacket pkt{};

    pkt.header.version = RDT_VERSION;
    pkt.header.magic = RDT_MAGIC;

    pkt.header.seq_num = seq;
    pkt.header.ack_num = 0;

    pkt.header.flags = RDTFlag::META;

    pkt.header.payload_len =
        static_cast<uint16_t>(
            sizeof(FileMetadata));

    std::memcpy(
        pkt.payload,
        &meta,
        sizeof(FileMetadata));

    fillChecksum(pkt);

    return pkt;
}