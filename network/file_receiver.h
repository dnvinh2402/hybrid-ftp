#ifndef FILE_RECEIVER_H
#define FILE_RECEIVER_H
#include "transfer_session.h"
#include <string>
#include "../common/rdt_packet.h"
#include "rdt_receiver.h"
#include "../common/logger.h"
#include "../common/file_metadata.h"
#include "packet_parser.h"
#include <atomic>
class RDTReceiver;

class FileReceiver
{
private:
    RDTReceiver &rdtReceiver;
    TransferSession session;
    static constexpr int MAX_CONSECUTIVE_TIMEOUTS = 10; // so lan timeout lientiep toi da cho phep
    std::atomic<bool> aborted{false};

public:
    explicit FileReceiver(RDTReceiver &receiver);
    void resetSession();
    bool receiveFile(const std::string& outputDir = "server_files");
    void printSummary() const;
    const TransferSession& getSession() const;
    void abortTransfer();
};

#endif