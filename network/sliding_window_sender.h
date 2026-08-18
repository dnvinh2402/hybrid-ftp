#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <atomic>
#include "../common/rdt_packet.h"

class UDPSocket;

// ============================================================
//  SlidingWindowSender — Go-Back-N (GBN) sender
//
//  Giao thức:
//    • Giữ tối đa WINDOW_SIZE gói "in-flight" (đã gửi, chưa ACK).
//    • ACK là cumulative: ACK(n) xác nhận tất cả seq <= n.
//    • Nếu timeout 1 gói trong cửa sổ → retransmit TẤT CẢ gói
//      từ base đến nextToSend (Go-Back-N).
//    • Không thay đổi RDTPacket / rdt_packet.h — dùng đúng
//      header format hiện tại.
//
//  Cách dùng (từ FileSender):
//    SlidingWindowSender sw(udpSocket);
//    sw.beginSession(ip, port);
//    sw.send(packet);   // gọi nhiều lần
//    sw.flush();        // đợi ACK hết gói còn lại trong cửa sổ
// ============================================================

class SlidingWindowSender
{
public:
    // Kích thước cửa sổ: 8 gói in-flight cùng lúc.
    // Với MAX_PAYLOAD_SIZE=1400 và RTT ~1ms LAN → ~11 MB/s throughput lý thuyết.
    // Có thể tăng lên 16 hoặc 32 nếu mạng cho phép.
    static constexpr int WINDOW_SIZE  = 16;

    // Timeout mỗi lần poll ACK (ms). Ngắn hơn timeout socket để
    // phát hiện loss sớm mà không phải chờ hết toàn bộ SO_RCVTIMEO.
    static constexpr int POLL_TIMEOUT_MS = 500;

    // Số lần toàn bộ cửa sổ bị timeout liên tiếp trước khi báo lỗi.
    static constexpr int MAX_WINDOW_RETRIES = 5;

    explicit SlidingWindowSender(UDPSocket &socket);

    // Bắt đầu phiên mới — phải gọi trước send() đầu tiên.
    void beginSession(const std::string &ip, unsigned short port);

    // Gửi 1 packet.  Nếu cửa sổ đầy, tự động block & drain ACK trước.
    // Trả false nếu hết retry (lỗi mạng không phục hồi được).
    bool send(const RDTPacket &packet);

    // Sau khi send() hết tất cả packet, gọi flush() để đợi ACK
    // cho những gói cuối còn nằm trong cửa sổ.
    bool flush();

    void abortTransfer();

private:
    UDPSocket &udp;

    std::string remoteIp;
    unsigned short remotePort = 0;

    // Circular buffer — lưu bản sao của các packet đã gửi nhưng chưa ACK.
    // Index = seq_num % WINDOW_SIZE
    std::vector<RDTPacket> window;           // size = WINDOW_SIZE
    std::vector<bool> acked;            // true = đã nhận ACK
    std::vector<std::chrono::steady_clock::time_point> sentTime; // thời điểm gửi

    std::atomic<bool> aborted{false};

    uint32_t base        = 0;  // seq nhỏ nhất chưa được ACK
    uint32_t nextToSend  = 0;  // seq tiếp theo sẽ gửi

    int windowRetries = 0;     // đếm số lần toàn bộ cửa sổ bị timeout

    // Đọc tất cả ACK có sẵn trong buffer socket (non-blocking nếu có thể).
    // Trả về số ACK hợp lệ nhận được.
    int drainAcks();

    // Gửi lại tất cả gói từ base đến nextToSend-1 (GBN retransmit).
    bool retransmitWindow();

    // Tính seq_num % WINDOW_SIZE để index vào circular buffer.
    static int slot(uint32_t seq) { 
    return static_cast<int>(seq % static_cast<uint32_t>(WINDOW_SIZE)); 
}
    // Kiểm tra xem cửa sổ có đầy không.
    bool windowFull() const { return (nextToSend - base) >= static_cast<uint32_t>(WINDOW_SIZE); }
};