#include "rdt_receiver.h"

#include "udp_socket.h"
#include "packet_builder.h"

#include "../common/rdt_packet.h"
#include "../common/logger.h"

RDTReceiver::RDTReceiver(UDPSocket &socket)
    : udp(socket),
    receiveCount(0),
      expectedSeq(0)
{
}

void RDTReceiver::reset()
{
    receiveCount = 0;
    expectedSeq = 0;
}

bool RDTReceiver::receive(
    RDTPacket& packet,
    std::string& senderIp,
    unsigned short& senderPort)
{
    while (true)
    {
        if (!udp.receivePacket(
                packet,
                senderIp,
                senderPort))
        {
            return false;
        }

        receiveCount++;

        log_info(
            "Receive packet #" +
            std::to_string(receiveCount));

        log_info(
            "From " +
            senderIp +
            ":" +
            std::to_string(senderPort));

        log_info(
            "SEQ = " +
            std::to_string(packet.header.seq_num));

        // =========================
        // 1. Version
        // =========================

        if (packet.header.version != RDT_VERSION)
        {
            log_error("Protocol version mismatch.");
            continue;
        }

        // =========================
        // 2. Magic
        // =========================

        if (packet.header.magic != RDT_MAGIC)
        {
            log_error("Invalid protocol magic.");
            continue;
        }

        // =========================
        // 3. Payload length
        // =========================

        if (packet.header.payload_len > MAX_PAYLOAD_SIZE)
        {
            log_error(
                "Payload too large: " +
                std::to_string(packet.header.payload_len));

            continue;
        }

        // =========================
        // 4. Checksum
        // =========================

        if (!verifyChecksum(packet))
        {
            log_error("Checksum error.");

            // Không ACK packet hỏng
            continue;
        }

        // =========================
        // 5. Flags
        // =========================

        const uint8_t flags = packet.header.flags;

        if (flags == 0)
        {
            log_error("Packet has no flags.");
            continue;
        }

        // Receiver không nhận ACK
        if (flags & RDTFlag::ACK)
        {
            log_error("Unexpected ACK packet.");
            continue;
        }

        bool isData =
            (flags & RDTFlag::DATA) != 0;

        bool isMeta =
            (flags & RDTFlag::META) != 0;

        bool isFin =
            (flags & RDTFlag::FIN) != 0;

        int packetTypeCount =
            static_cast<int>(isData) +
            static_cast<int>(isMeta) +
            static_cast<int>(isFin);

        if (packetTypeCount != 1)
        {
            log_error("Invalid packet flags.");
            continue;
        }

        // =========================
        // 6. Sequence number
        // =========================

        uint32_t seq = packet.header.seq_num;

        // Packet mới
        if (seq == expectedSeq)
        {
            if (!sendAck(
                    seq,
                    senderIp,
                    senderPort))
            {
                log_error("Failed to send ACK.");
                return false;
            }

            log_info(
                "ACK sent for SEQ " +
                std::to_string(seq));

            expectedSeq++;

            return true;
        }

        // =========================
        // 7. Duplicate packet
        // =========================

        if (seq < expectedSeq)
        {
            log_info(
                "Duplicate packet SEQ=" +
                std::to_string(seq) +
                ". Resending ACK.");

            if (!sendAck(
                    seq,
                    senderIp,
                    senderPort))
            {
                log_error("Failed to resend ACK.");
                return false;
            }

            continue;
        }

        // =========================
        // 8. Packet đến quá sớm
        // =========================

        log_error(
            "Unexpected future SEQ=" +
            std::to_string(seq) +
            ", expected=" +
            std::to_string(expectedSeq));

        continue;
    }
}
bool RDTReceiver::sendAck(
    uint32_t seq,
    const std::string &ip,
    unsigned short port)
{
    RDTPacket ack =
        PacketBuilder::buildAckPacket(seq);

    return udp.sendPacket(
        ack,
        ip,
        port);
}