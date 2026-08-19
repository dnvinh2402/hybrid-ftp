#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "../common/rdt_packet.h"

class UDPSocket;

// Go-Back-N sender with cumulative ACKs and timeout-based retransmission.
class SlidingWindowSender
{
public:
    // Maximum number of packets that may be in flight at once.
    static constexpr int WINDOW_SIZE = 16;

    // Short ACK polling interval. Retransmission happens after several polls.
    static constexpr int POLL_TIMEOUT_MS = 500;

    static constexpr int DEFAULT_MAX_WINDOW_RETRIES = 5;

    explicit SlidingWindowSender(
        UDPSocket &socket,
        int maxWindowRetries = DEFAULT_MAX_WINDOW_RETRIES);

    void beginSession(const std::string &ip, unsigned short port);

    bool send(const RDTPacket &packet);

    bool flush();

    void abortTransfer();

private:
    UDPSocket &udp;

    std::string remoteIp;
    unsigned short remotePort = 0;

    std::vector<RDTPacket> window;
    std::vector<bool> acked;
    std::vector<std::chrono::steady_clock::time_point> sentTime;

    std::atomic<bool> aborted{false};

    uint32_t base = 0;
    uint32_t nextToSend = 0;

    int windowRetries = 0;
    int maxWindowRetries = DEFAULT_MAX_WINDOW_RETRIES;

    int drainAcks();

    bool retransmitWindow();

    static int slot(uint32_t seq)
    {
        return static_cast<int>(
            seq % static_cast<uint32_t>(WINDOW_SIZE));
    }

    bool windowFull() const
    {
        return (nextToSend - base) >=
               static_cast<uint32_t>(WINDOW_SIZE);
    }
};