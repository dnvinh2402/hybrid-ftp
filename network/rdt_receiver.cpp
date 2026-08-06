#include "rdt_receiver.h"
#include "udp_socket.h"
#include "packet_builder.h"
#include "../common/rdt_packet.h"
#include "../common/logger.h"

RDTReceiver::RDTReceiver(UDPSocket &socket)
    : udp(socket)
{
}

bool RDTReceiver::receive(RDTPacket &packet,
                          std::string &senderIp,
                          unsigned short &senderPort)
{
    static bool firstPacket = true;
    static int receiveCount = 0;

    if (!udp.receivePacket(packet, senderIp, senderPort))
        return false;

    receiveCount++;

    log_info("Receive packet #" + std::to_string(receiveCount));
    log_info("SEQ = " + std::to_string(packet.header.seq_num));

    if (!verifyChecksum(packet))
    {
        log_error("Checksum error.");
        return false;
    }

    // --------- Chỉ dùng để TEST Retransmission ---------
    if (firstPacket)
    {
        firstPacket = false;

        log_info("Simulate ACK loss (Do not send ACK)");

        // Không gửi ACK
        return true;
    }
    // ---------------------------------------------------
    if (sendAck(packet.header.seq_num, senderIp, senderPort))
    {
        log_info("ACK sent successfully.");
    }
    else
    {
        log_error("Failed to send ACK.");
    }
    return true;
}
bool RDTReceiver::sendAck(uint32_t seq,
                          const std::string &ip,
                          unsigned short port)
{
    auto ack = PacketBuilder::buildAckPacket(seq);

    return udp.sendPacket(ack, ip, port);
}