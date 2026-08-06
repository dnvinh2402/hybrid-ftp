#ifndef FILE_RECEIVER_H
#define FILE_RECEIVER_H
#include "transfer_session.h"
#include <string>
#include "../common/rdt_packet.h"
#include "rdt_receiver.h"
#include "../common/logger.h"
#include "../common/rdt_packet.h"
class RDTReceiver;

class FileReceiver
{
private:
    RDTReceiver &rdtReceiver;
    TransferSession session;
    void resetSession();
    void printSummary() const;
    bool isUnexpectedPacket(const RDTPacket& packet);

public:
    explicit FileReceiver(RDTReceiver &receiver);

    bool receiveFile(const std::string &outputFile);
    const TransferSession& getSession() const;
};

#endif