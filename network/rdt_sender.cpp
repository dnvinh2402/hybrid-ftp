#include "rdt_sender.h"
#include "udp_socket.h"
#include "../common/rdt_packet.h"
#include "../common/logger.h"
RDTSender::RDTSender(UDPSocket &socket, int maxRetry)
    : udp(socket),
      maxRetry(maxRetry > 0 ? maxRetry : 1)
{
}

bool RDTSender::send(const RDTPacket &packet,
                     const std::string &ip,
                     unsigned short port)
{
    const uint32_t seq = packet.header.seq_num;

    for (int retry = 1; retry <= maxRetry; retry++)
    {
        log_debug("Send seq=" + std::to_string(seq) +
                 " attempt=" + std::to_string(retry) +
                 "/" + std::to_string(maxRetry));

        if (!udp.sendPacket(packet, ip, port))
        {
            log_error("UDP send failed for seq=" + std::to_string(seq));
            return false;
        }

        if (waitAck(seq))
        {
            log_debug("ACK received for seq=" + std::to_string(seq));
            return true;
        }

        if (retry < maxRetry)
            log_info("Timeout/bad ACK for seq=" + std::to_string(seq) +
                     ". Retransmitting...");
    }

    log_error("Max retries reached for seq=" + std::to_string(seq));
    return false;
}

bool RDTSender::waitAck(uint32_t expectedSeq)
{
    // Đọc tối đa MAX_RETRY lần để bỏ qua ACK stale (ack_num không khớp)
    // còn sót lại trong buffer socket từ lần retransmit trước.
    // Nếu gặp ACK đúng ack_num thì trả true ngay lập tức.
    // Nếu hết lần đọc mà vẫn không khớp thì trả false để tầng trên retry.
    for (int staleSkip = 0; staleSkip < MAX_STALE_ACK_SKIP; staleSkip++)
    {
        RDTPacket ack;
        std::string ip;
        unsigned short port;

        if (!udp.receivePacket(ack, ip, port))
        {
            log_error("Timeout waiting ACK for seq=" + std::to_string(expectedSeq));
            return false;   // timeout thật sự -- tầng trên sẽ retransmit
        }

        if (!verifyChecksum(ack))
        {
            log_error("ACK checksum error (skipping stale).");
            continue;       // đọc tiếp, bỏ qua gói corrupt
        }

        if (!(ack.header.flags & RDTFlag::ACK))
        {
            log_error("Received non-ACK packet while waiting for ACK (seq=" +
                      std::to_string(expectedSeq) + ").");
            continue;
        }

        if (ack.header.version != RDT_VERSION)
        {
            log_error("ACK version mismatch.");
            continue;
        }

        if (ack.header.magic != RDT_MAGIC)
        {
            log_error("ACK magic mismatch.");
            continue;
        }

        if (ack.header.ack_num != expectedSeq)
        {
            // ACK stale từ lần retransmit cũ -- đọc tiếp thay vì return false.
            log_debug("Stale ACK ack_num=" + std::to_string(ack.header.ack_num) +
                     " expected=" + std::to_string(expectedSeq) + ", skipping.");
            continue;
        }

        return true;   // ACK đúng seq, đúng flags, đúng checksum
    }

    log_error("Gave up waiting: too many stale ACKs for seq=" +
              std::to_string(expectedSeq));
    return false;
}