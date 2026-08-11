#include "rdt_receiver.h"
#include "udp_socket.h"
#include "packet_builder.h"
#include "../common/rdt_packet.h"
#include "../common/logger.h"

RDTReceiver::RDTReceiver(UDPSocket &socket, bool simulateAckLoss)
    : udp(socket), simulateAckLoss(simulateAckLoss)
{
}

bool RDTReceiver::receive(RDTPacket &packet,
                          std::string &senderIp,
                          unsigned short &senderPort)
{
    // static bool firstPacket = true;
    // static int receiveCount = 0;

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

   

    if (simulateAckLoss && firstPacketOfSession)
    {
        firstPacketOfSession = false;
 
        log_info("[TEST] Simulate ACK loss for first packet (config.simulateAckLoss=true).");
 
        return true; 
    }

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