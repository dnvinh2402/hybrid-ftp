#pragma once
#include <cstdint>
#include <string>

class UDPSocket;
struct RDTPacket;

class RDTReceiver
{
private:
    UDPSocket &udp;
    int receiveCount = 0;
    bool firstPacketOfSession = true;

    // Bật/tắt việc CHỦ ĐỘNG bỏ qua gửi ACK cho gói đầu tiên của MỖI phiên
    // truyền -- chỉ dùng khi cần kiểm thử cơ chế retransmit của bên gửi.
    bool simulateAckLoss;
public:
    explicit RDTReceiver(UDPSocket &socket, bool simulateAckLoss = false);

    bool receive(RDTPacket &packet,
                 std::string &senderIp,
                 unsigned short &senderPort);
    bool sendAck(uint32_t seq,
                 const std::string &ip,
                 unsigned short port);
    void resetSession();
};