#pragma once
#include <cstdint>
#include <string>

class UDPSocket;
struct RDTPacket;

class RDTSender
{
private:
    UDPSocket &udp;

    // Số lần gửi lại tối đa cho 1 packet trước khi báo lỗi
    static constexpr int MAX_RETRY = 5;

    // Số lần đọc tối đa để bỏ qua ACK stale (ack_num không khớp) còn
    // sót trong buffer socket -- nhỏ hơn hoặc bằng MAX_RETRY là đủ an toàn.
    static constexpr int MAX_STALE_ACK_SKIP = 3;

    // Chờ ACK đúng expectedSeq.  Nếu gặp ACK stale (ack_num lệch) thì
    // đọc tiếp thay vì trả false ngay -- tránh false-timeout do gói cũ.
    bool waitAck(uint32_t expectedSeq);

public:
    explicit RDTSender(UDPSocket &socket);

    // Gửi 1 packet và đợi ACK.  Tự động retry tối đa MAX_RETRY lần.
    // Trả true khi ACK đúng nhận được, false khi hết retry hoặc lỗi mạng.
    bool send(const RDTPacket &packet,
              const std::string &ip,
              unsigned short port);
};