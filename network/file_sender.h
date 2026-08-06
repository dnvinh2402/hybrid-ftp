#ifndef FILE_SENDER_H
#define FILE_SENDER_H
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