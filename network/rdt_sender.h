#pragma once

#include <string>
#include <cstdint>
class UDPSocket;
struct RDTPacket;

class RDTSender
{
public:

    explicit RDTSender(UDPSocket& socket);

    bool send(const RDTPacket& packet,
              const std::string& ip,
              unsigned short port);
private:
    static constexpr int MAX_RETRY = 5;
    UDPSocket& udp;
    bool waitAck(uint32_t seq);
};