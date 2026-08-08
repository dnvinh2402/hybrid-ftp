#ifndef FILE_RECEIVER_H
#define FILE_RECEIVER_H
#include "transfer_session.h"
#include <string>
#include "../common/rdt_packet.h"
#include "rdt_receiver.h"
#include "../common/logger.h"
#include "../common/rdt_packet.h"
#include "../common/file_metadata.h"
#include "packet_parser.h"
class RDTReceiver;

class FileReceiver
{
private:
    RDTReceiver &rdtReceiver;
    TransferSession session;
    void resetSession();
    void printSummary() const;
    static constexpr int MAX_CONSECUTIVE_TIMEOUTS = 10; // so lan timeout lientiep toi da cho phep

public:
    explicit FileReceiver(RDTReceiver &receiver);

    bool receiveFile();
    const TransferSession& getSession() const;
};

#endif