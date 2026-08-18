#pragma once
#include <string>
#include "../common/file_metadata.h"
#include "packet_builder.h"
#include "rdt_sender.h"
#include "transfer_session.h"
#include <atomic>
// Forward declaration để tránh include vòng
class SlidingWindowSender;


class FileSender
{
private:
    RDTSender &rdtSender;

    // Con trỏ tùy chọn tới SlidingWindowSender.
    // nullptr  → dùng Stop-and-Wait (rdtSender).
    // non-null → dùng Go-Back-N.
    // Được set bởi DataChannel sau khi tạo FileSender.
    SlidingWindowSender *swSender = nullptr;

    TransferSession session;

    void resetSession();
    void printSummary(double elapsedSec = 0.0) const;
    std::atomic<bool> aborted{false};
public:
    explicit FileSender(RDTSender &sender);

    // Kích hoạt chế độ Go-Back-N.  Gọi từ DataChannel::open().
    // Truyền nullptr để quay về Stop-and-Wait.
    void setWindowSender(SlidingWindowSender *sw);

    bool sendFile(const std::string &filePath,
                  const std::string &receiverIp,
                  unsigned short receiverPort);

    const TransferSession &getSession() const;
    void abortTransfer();
};