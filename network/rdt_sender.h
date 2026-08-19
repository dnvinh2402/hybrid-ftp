#pragma once

#include <cstdint>
#include <string>

class UDPSocket;
struct RDTPacket;

class RDTSender
{
private:
    UDPSocket &udp;
    int maxRetry;

    // Maximum stale ACKs to skip while waiting for the expected ACK.
    static constexpr int MAX_STALE_ACK_SKIP = 3;

    bool waitAck(uint32_t expectedSeq);

public:
    explicit RDTSender(UDPSocket &socket, int maxRetry = 5);

    // Stop-and-Wait: send one packet and wait for its matching ACK.
    bool send(const RDTPacket &packet,
              const std::string &ip,
              unsigned short port);
};