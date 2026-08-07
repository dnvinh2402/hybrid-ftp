#include "rdt_sender.h"
#include "udp_socket.h"
#include "../common/rdt_packet.h"
#include "../common/logger.h"
RDTSender::RDTSender(UDPSocket &socket)
    : udp(socket)
{
}

bool RDTSender::send(const RDTPacket &packet,
                     const std::string &ip,
                     unsigned short port)
{
    for (int retry = 1; retry <= MAX_RETRY; retry++)
    {
        log_info(
            "Attempt " + std::to_string(retry) + "/" + std::to_string(MAX_RETRY));

        if (!udp.sendPacket(packet, ip, port))
        {
            log_error("UDP send failed.");
            return false;
        }

        if (waitAck(
                packet.header.seq_num,
                ip,
                port))
        {
            log_info("ACK received.");
            return true;
        }

        log_info("Timeout. Retransmitting...");
    }

    log_error("Maximum retry reached.");

    return false;
}
bool RDTSender::waitAck(
    uint32_t seq,
    const std::string& expectedIp,
    unsigned short expectedPort)
{
    while (true)
    {
        RDTPacket ack;

        std::string ip;
        unsigned short port;

        if (!udp.receivePacket(ack, ip, port))
        {
            log_error("Timeout waiting ACK.");
            return false;
        }

        if (ip != expectedIp || port != expectedPort)
        {
            log_info("Ignoring ACK from unexpected endpoint.");
            continue;
        }

        if (!verifyChecksum(ack))
        {
            log_error("ACK checksum error.");
            continue;
        }

        if (ack.header.version != RDT_VERSION)
        {
            log_error("Protocol version mismatch.");
            continue;
        }

        if (ack.header.magic != RDT_MAGIC)
        {
            log_error("Invalid protocol.");
            continue;
        }

        if (!(ack.header.flags & RDTFlag::ACK))
        {
            log_info("Ignoring non-ACK packet.");
            continue;
        }

        if (ack.header.ack_num != seq)
        {
            log_info(
                "Ignoring ACK " +
                std::to_string(ack.header.ack_num) +
                ", waiting for ACK " +
                std::to_string(seq));

            continue;
        }

        return true;
    }
}