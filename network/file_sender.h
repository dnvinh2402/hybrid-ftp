#ifndef FILE_SENDER_H
#define FILE_SENDER_H
#include "transfer_session.h"
#include "../common/file_metadata.h"
#include "file_sender.h"
#include "rdt_sender.h"
#include "packet_builder.h"
#include "../common/logger.h"
#include "../common/rdt_packet.h"
#include "transfer_session.h"
#include <string>

class RDTSender;

class FileSender
{
private:
    RDTSender &rdtSender;
    TransferSession session;

    void resetSession();

    void printSummary() const;

public:
    explicit FileSender(RDTSender &sender);

    bool sendFile(const std::string &filePath,
                  const std::string &receiverIp,
                  unsigned short receiverPort);

    const TransferSession& getSession() const;
};

#endif