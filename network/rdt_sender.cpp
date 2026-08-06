#include "rdt_sender.h"
#include "udp_socket.h"
#include "../common/rdt_packet.h"
#include "logger.h"
RDTSender::RDTSender(UDPSocket &socket)
    : udp(socket)
{
}

bool RDTSender::send(const RDTPacket& packet,
                     const std::string& ip,
                     unsigned short port)
{
    for (int retry = 1; retry <= MAX_RETRY; retry++)
    {
        log_info(
            "Attempt "
            + std::to_string(retry)
            + "/"
            + std::to_string(MAX_RETRY));

        if (!udp.sendPacket(packet, ip, port))
            return false;

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
        return false;

    if (!verifyChecksum(ack))
        return false;

    if (!(ack.header.flags & RDTFlag::ACK))
        return false;

    if (ack.header.ack_num != seq)
        return false;

    return true;
}