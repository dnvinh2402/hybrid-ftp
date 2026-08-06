#ifndef FILE_RECEIVER_H
#define FILE_RECEIVER_H
#include "transfer_session.h"
#include <string>
#include "../common/rdt_packet.h"
#include "rdt_receiver.h"
#include "../common/logger.h"
#include "../common/rdt_packet.h"
#include "file_metadata.h"
#include "packet_parser.h"
class RDTReceiver;

class FileReceiver
{
private:
    RDTReceiver &rdtReceiver;
    TransferSession session;
    void resetSession();
    void printSummary() const;
    bool isUnexpectedSequence(const RDTPacket& packet);

public:
    explicit FileReceiver(RDTReceiver &receiver);

    bool receiveFile();
    const TransferSession& getSession() const;
};

#endif