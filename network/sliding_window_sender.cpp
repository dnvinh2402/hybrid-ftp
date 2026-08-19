#include "sliding_window_sender.h"
#include "udp_socket.h"
#include "packet_builder.h"
#include "../common/logger.h"
#include <chrono>
#include <thread>

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;

SlidingWindowSender::SlidingWindowSender(UDPSocket &socket)
    : udp(socket),
      window(WINDOW_SIZE),
      acked(WINDOW_SIZE, false),
      sentTime(WINDOW_SIZE)
{
}

void SlidingWindowSender::beginSession(const std::string &ip, unsigned short port)
{
    remoteIp = ip;
    remotePort = port;
    base = 0;
    nextToSend = 0;
    windowRetries = 0;
    aborted.store(false);
    std::fill(acked.begin(), acked.end(), false);
}
void SlidingWindowSender::abortTransfer()
{
    aborted.store(true);
}
// ------------------------------------------------------------
//  drainAcks() — đọc tất cả ACK đang có trong buffer socket.
//
//  Chiến lược: gọi receivePacket() với timeout ngắn (POLL_TIMEOUT_MS)
//  lặp lại cho đến khi timeout (không còn ACK nào), hoặc
//  nhận được ACK đẩy base về đúng nextToSend (cửa sổ trống).
//
//  Trả về số ACK hợp lệ đã xử lý.
// ------------------------------------------------------------
int SlidingWindowSender::drainAcks()
{
    int count = 0;

    // Đặt timeout socket xuống POLL_TIMEOUT_MS để poll nhanh.
    // Sau khi drain xong sẽ restore lại 5000ms.
    udp.setReceiveTimeout(POLL_TIMEOUT_MS);

    while (true)
    {
        RDTPacket ack;
        std::string ip;
        unsigned short port;

        if (!udp.receivePacket(ack, ip, port))
            break; // timeout → không còn ACK nào trong buffer

        // Bỏ qua gói corrupt hoặc không phải ACK
        if (!verifyChecksum(ack))
        {
            log_error("[GBN] Corrupt ACK discarded.");
            continue;
        }
        if (!(ack.header.flags & RDTFlag::ACK))
        {
            log_error("[GBN] Non-ACK packet received while draining, discarded.");
            continue;
        }
        if (ack.header.version != RDT_VERSION || ack.header.magic != RDT_MAGIC)
        {
            log_error("[GBN] ACK version/magic mismatch, discarded.");
            continue;
        }

        uint32_t ackedSeq = ack.header.ack_num;

        // Bỏ qua ACK nằm ngoài cửa sổ hiện tại (stale từ phiên cũ
        // hoặc đã được xử lý trước đó).
        if (ackedSeq < base || ackedSeq >= nextToSend)
        {
            // tatlog
            log_info("[GBN] Stale/out-of-window ACK seq=" +
                     std::to_string(ackedSeq) + " (base=" +
                     std::to_string(base) + " next=" +
                     std::to_string(nextToSend) + "), ignored.");
            continue;
        }

        // Đánh dấu slot này đã được ACK
        if (ackedSeq >= base && ackedSeq < nextToSend)
        {
            //tatlog
            // log_info("[GBN] ACK received seq=" +
            //          std::to_string(ackedSeq) +
            //          " (base=" + std::to_string(base) + ")");

            // ACK cumulative:
            // ACK(n) xác nhận tất cả packet từ base đến n.
            while (base <= ackedSeq && base < nextToSend)
            {
                acked[slot(base)] = false;
                base++;
            }

            count++;
        }

        // Nếu cửa sổ đã trống thì không cần đọc tiếp
        if (base == nextToSend)
            break;
    }

    // Restore timeout socket về 5000ms (giá trị dài để gọi ngoài vẫn OK)
    udp.setReceiveTimeout(5000);

    return count;
}

// ------------------------------------------------------------
//  retransmitWindow() — GBN: gửi lại TẤT CẢ gói từ base đến
//  nextToSend-1 theo đúng thứ tự. Reset sentTime để timeout
//  được tính lại từ đầu sau khi retransmit.
// ------------------------------------------------------------
bool SlidingWindowSender::retransmitWindow()
{
    if (base == nextToSend)
        return true; // cửa sổ trống, không cần retransmit

    windowRetries++;
    if (windowRetries > MAX_WINDOW_RETRIES)
    {
        log_error("[GBN] Max retries (" + std::to_string(MAX_WINDOW_RETRIES) +
                  ") exceeded. Giving up at base=" + std::to_string(base));
        return false;
    }

    log_info("[GBN] Window timeout (retry " + std::to_string(windowRetries) +
             "/" + std::to_string(MAX_WINDOW_RETRIES) +
             "). Retransmitting seq=" + std::to_string(base) +
             " to " + std::to_string(nextToSend - 1));

    auto now = Clock::now();
    for (uint32_t seq = base; seq < nextToSend; seq++)
    {
        if (!udp.sendPacket(window[slot(seq)], remoteIp, remotePort))
        {
            log_error("[GBN] UDP send failed during retransmit (seq=" +
                      std::to_string(seq) + ").");
            return false;
        }
        sentTime[slot(seq)] = now;
        log_info("[GBN] Retransmit seq=" + std::to_string(seq));
    }

    return true;
}

// ------------------------------------------------------------
//  send() — công khai, gọi từ FileSender cho mỗi packet.
//
//  Nếu cửa sổ đầy: poll ACK cho đến khi có chỗ trống hoặc
//  timeout → retransmit → thử lại.
// ------------------------------------------------------------
bool SlidingWindowSender::send(const RDTPacket &packet)
{
    // Đợi cho đến khi cửa sổ có chỗ trống
    while (windowFull())
    {
        if (aborted.load())
        {
            log_info("[GBN] Sender aborted during windowFull wait.");
            return false;
        }
        int got = drainAcks();
        if (got > 0)
        {
            windowRetries = 0; // nhận được ACK → reset bộ đếm retry
            continue;
        }

        // Không nhận được ACK nào → kiểm tra timeout của từng slot
        // (trong GBN: nếu bất kỳ slot nào timeout → retransmit toàn bộ)
        bool anyTimeout = false;
        auto now = Clock::now();
        for (uint32_t seq = base; seq < nextToSend; seq++)
        {
            auto elapsed = std::chrono::duration_cast<Ms>(
                               now - sentTime[slot(seq)])
                               .count();
            if (elapsed >= POLL_TIMEOUT_MS * 5) // 1000ms hard timeout per packet
            {
                anyTimeout = true;
                break;
            }
        }

        if (anyTimeout)
        {
            if (!retransmitWindow())
                return false;
        }
    }

    // Có chỗ trống trong cửa sổ → gửi packet mới
    int s = slot(nextToSend);
    window[s] = packet; // lưu bản sao để retransmit nếu cần
    acked[s] = false;

    if (!udp.sendPacket(packet, remoteIp, remotePort))
    {
        log_error("[GBN] UDP send failed for seq=" +
                  std::to_string(packet.header.seq_num));
        return false;
    }

    sentTime[s] = Clock::now();
    windowRetries = 0;
    // tatlog
    // log_info("[GBN] Sent seq=" + std::to_string(packet.header.seq_num) +
    //          " (window: base=" + std::to_string(base) +
    //          " inFlight=" + std::to_string(nextToSend - base + 1) + ")");

    nextToSend++;

    // Ngay sau khi gửi, thử drain ACK đang chờ để mở rộng cửa sổ sớm
    // drainAcks();

    return true;
}

// ------------------------------------------------------------
//  flush() — đợi ACK cho tất cả gói còn lại trong cửa sổ.
//  Gọi sau khi send() tất cả packet kể cả FIN.
// ------------------------------------------------------------
bool SlidingWindowSender::flush()
{
    log_info("[GBN] Flushing window (base=" + std::to_string(base) +
             " nextToSend=" + std::to_string(nextToSend) + ")");

    while (base < nextToSend)
    {
        if (aborted.load())
        {
            log_info("[GBN] Sender aborted during flush.");
            return false;
        }
        int got = drainAcks();
        if (got > 0)
        {
            windowRetries = 0;
            continue;
        }

        // // Timeout → retransmit
        // if (!retransmitWindow())
        //     return false;

        // // Sau retransmit, đọc ACK một lần nữa
        // drainAcks();
        // Bổ sung kiểm tra timeout THỰC SỰ
        bool anyTimeout = false;
        auto now = Clock::now();
        for (uint32_t seq = base; seq < nextToSend; seq++)
        {
            auto elapsed = std::chrono::duration_cast<Ms>(
                               now - sentTime[slot(seq)])
                               .count();

            // Chờ đủ 1000ms (POLL_TIMEOUT_MS * 5) mới tính là timeout
            if (elapsed >= POLL_TIMEOUT_MS * 5)
            {
                anyTimeout = true;
                break;
            }
        }

        if (anyTimeout)
        {
            if (!retransmitWindow())
                return false;
        }
    }

    log_info("[GBN] Flush complete. All packets ACKed.");
    return true;
}