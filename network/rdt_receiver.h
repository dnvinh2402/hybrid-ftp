#pragma once
#include <cstdint>
#include <string>

class UDPSocket;
struct RDTPacket;

class RDTReceiver
{
private:
    UDPSocket &udp;
    uint64_t receiveCount;
    uint32_t expectedSeq;

public:
    explicit RDTReceiver(UDPSocket &socket);

    bool receive(RDTPacket &packet,
                 std::string &senderIp,
                 unsigned short &senderPort);
    bool sendAck(uint32_t seq,
                 const std::string &ip,
                 unsigned short port);
    void reset();
};