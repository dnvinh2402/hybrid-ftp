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

        if (waitAck(packet.header.seq_num))
        {
            log_info("ACK received.");
            return true;
        }

        log_info("Timeout. Retransmitting...");
    }

    log_error("Maximum retry reached.");

    return false;
}
bool RDTSender::waitAck(uint32_t seq)
{
    RDTPacket ack;

    std::string ip;

    unsigned short port;

    if (!udp.receivePacket(ack, ip, port))
    {
        log_error("Timeout waiting ACK.");
        return false;
    }
    if (!verifyChecksum(ack))
    {
        log_error("ACK checksum error.");
        return false;
    }

    if (!(ack.header.flags & RDTFlag::ACK))
    {
        log_error("Not an ACK packet.");
        return false;
    }
    if (ack.header.ack_num != seq)
    {
        log_error("Unexpected ACK.");
        return false;
    }
    if (ack.header.version != RDT_VERSION)
    {
        log_error("Protocol version mismatch.");
        return false;
    }

    if (ack.header.magic != RDT_MAGIC)
    {
        log_error("Invalid protocol.");
        return false;
    }
    return true;
}