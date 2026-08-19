#include "rdt_receiver.h"

#include "packet_builder.h"
#include "udp_socket.h"

#include "../common/logger.h"
#include "../common/rdt_packet.h"

RDTReceiver::RDTReceiver(
    UDPSocket &socket,
    bool simulateAckLoss)
    : udp(socket),
      simulateAckLoss(simulateAckLoss)
{
}

void RDTReceiver::resetSession()
{
    receiveCount = 0;
    firstPacketOfSession = true;
}

bool RDTReceiver::receive(
    RDTPacket &packet,
    std::string &senderIp,
    unsigned short &senderPort)
{
    if (!udp.receivePacket(packet, senderIp, senderPort))
    {
        return false;
    }

    receiveCount++;

    if (!verifyChecksum(packet))
    {
        log_error("Checksum error.");
        return false;
    }

    // ACK policy intentionally lives in FileReceiver.
    // FileReceiver knows whether a packet is in-order, duplicate, or
    // out-of-order and can therefore implement correct cumulative ACK
    // behavior for Go-Back-N. ACKing here would incorrectly acknowledge
    // packets that FileReceiver may later discard.
    return true;
}

bool RDTReceiver::sendAck(
    uint32_t seq,
    const std::string &ip,
    unsigned short port)
{
    // Optional deterministic ACK-loss injection for reliability tests.
    // Drop only the first ACK decision of each transfer session.
    if (simulateAckLoss && firstPacketOfSession)
    {
        firstPacketOfSession = false;
        log_info(
            "[TEST] Simulate ACK loss for ACK seq="
            + std::to_string(seq)
            + ".");
        return true;
    }

    firstPacketOfSession = false;

    auto ack = PacketBuilder::buildAckPacket(seq);
    return udp.sendPacket(ack, ip, port);
}