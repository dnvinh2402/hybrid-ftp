#include "sliding_window_sender.h"
#include "udp_socket.h"
#include "packet_builder.h"
#include "../common/logger.h"
#include <algorithm>
#include <chrono>

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::milliseconds;

SlidingWindowSender::SlidingWindowSender(
    UDPSocket &socket,
    int maxWindowRetries)
    : udp(socket),
      window(WINDOW_SIZE),
      acked(WINDOW_SIZE, false),
      sentTime(WINDOW_SIZE),
      maxWindowRetries(maxWindowRetries > 0
                           ? maxWindowRetries
                           : DEFAULT_MAX_WINDOW_RETRIES)
{
}

void SlidingWindowSender::abortTransfer()
{
    aborted.store(true);
}

void SlidingWindowSender::beginSession(const std::string &ip, unsigned short port)
{
    remoteIp      = ip;
    remotePort    = port;
    base          = 0;
    nextToSend    = 0;
    windowRetries = 0;
    aborted.store(false);
    std::fill(acked.begin(), acked.end(), false);

    // Drain stale ACKs from a previous transfer before starting a new session.
    // Sau khi ABOR hoặc phiên cũ kết thúc, có thể còn ACK cũ
    // (seq từ phiên trước) nằm trong buffer OS. Nếu không xả,
    // drainAcks() của phiên mới sẽ đọc nhầm → stale ACK spam log
    // và làm flush() chậm (phải đợi timeout sau mỗi lần đọc stale).
    //
    // Cách xả: set timeout = 1ms, đọc cho đến khi timeout.
    udp.setReceiveTimeout(1);
    {
        RDTPacket dummy;
        std::string dip;
        unsigned short dport;
        int drained = 0;
        while (udp.receivePacket(dummy, dip, dport))
            drained++;
        if (drained > 0)
            log_info("[GBN] beginSession: drained " +
                     std::to_string(drained) +
                     " stale packet(s) from previous session.");
    }
    // Restore về POLL_TIMEOUT_MS cho phiên mới
    udp.setReceiveTimeout(POLL_TIMEOUT_MS);
}

// Read all currently available cumulative ACKs. The short socket timeout
// is configured once in beginSession() so an empty ACK queue does not block
// the sender for the full transfer timeout.
int SlidingWindowSender::drainAcks()
{
    int count = 0;

    while (true)
    {
        RDTPacket ack;
        std::string ip;
        unsigned short port;

        if (!udp.receivePacket(ack, ip, port))
            break;  // timeout POLL_TIMEOUT_MS → không còn ACK trong buffer

        if (!verifyChecksum(ack))                        continue;
        if (!(ack.header.flags & RDTFlag::ACK))          continue;
        if (ack.header.version != RDT_VERSION)           continue;
        if (ack.header.magic   != RDT_MAGIC)             continue;

        uint32_t ackedSeq = ack.header.ack_num;

        // Bỏ qua ACK nằm ngoài cửa sổ (ACK cũ hoặc ACK chưa gửi)
        if (ackedSeq < base || ackedSeq >= nextToSend)
            continue;

        // Cumulative ACK: Dịch chuyển base lên ackedSeq + 1
        while (base <= ackedSeq && base < nextToSend)
        {
            acked[slot(base)] = false;
            base++;
        }
        count++;

        if (base == nextToSend) break;  // Cửa sổ đã trống
    }

    return count;
}

bool SlidingWindowSender::retransmitWindow()
{
    if (base == nextToSend) return true;

    windowRetries++;
    if (windowRetries > maxWindowRetries)
    {
        log_error("[GBN] Max retries (" + std::to_string(maxWindowRetries) +
                  ") exceeded at base=" + std::to_string(base));
        return false;
    }

    log_info("[GBN] Timeout — retransmit seq=" + std::to_string(base) +
             ".." + std::to_string(nextToSend - 1) +
             " (retry " + std::to_string(windowRetries) +
             "/" + std::to_string(maxWindowRetries) + ")");

    auto now = Clock::now();
    for (uint32_t seq = base; seq < nextToSend; seq++)
    {
        if (!udp.sendPacket(window[slot(seq)], remoteIp, remotePort))
        {
            log_error("[GBN] UDP send failed retransmit seq=" +
                      std::to_string(seq));
            return false;
        }
        sentTime[slot(seq)] = now;
    }
    return true;
}

bool SlidingWindowSender::send(const RDTPacket &packet)
{
    if (aborted.load()) return false;

    while (windowFull())
    {
        if (aborted.load()) return false;

        int got = drainAcks();
        if (got > 0) { windowRetries = 0; continue; }

        // Kiểm tra timeout thực sự của base packet
        auto elapsed = std::chrono::duration_cast<Ms>(
            Clock::now() - sentTime[slot(base)]).count();
        if (elapsed >= POLL_TIMEOUT_MS * 5)
        {
            if (!retransmitWindow()) return false;
        }
    }

    int s         = slot(nextToSend);
    window[s]     = packet;
    acked[s]      = false;
    sentTime[s]   = Clock::now();
    windowRetries = 0;

    if (!udp.sendPacket(packet, remoteIp, remotePort))
    {
        log_error("[GBN] UDP send failed seq=" +
                  std::to_string(packet.header.seq_num));
        return false;
    }

    nextToSend++;
    return true;
}

bool SlidingWindowSender::flush()
{
    log_info("[GBN] Flushing " +
             std::to_string(nextToSend - base) +
             " packets in window...");

    while (base < nextToSend)
    {
        if (aborted.load()) return false;

        int got = drainAcks();
        if (got > 0) { windowRetries = 0; continue; }

        auto elapsed = std::chrono::duration_cast<Ms>(
            Clock::now() - sentTime[slot(base)]).count();
        if (elapsed >= POLL_TIMEOUT_MS * 5)
        {
            if (!retransmitWindow()) return false;
        }
    }

    log_info("[GBN] Flush complete. All packets ACKed.");
    return true;
}