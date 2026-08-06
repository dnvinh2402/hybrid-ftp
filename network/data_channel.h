#ifndef DATA_CHANNEL_H
#define DATA_CHANNEL_H

#include <string>
#include "data_channel_config.h"
#include "transfer_session.h"
class UDPSocket;
class RDTSender;
class RDTReceiver;
class FileSender;
class FileReceiver;

class DataChannel
{
public:
    DataChannel();

    ~DataChannel();

    bool open(const DataChannelConfig &config);

    void close();

    bool sendFile(const std::string &file,
                  const std::string &ip,
                  unsigned short port);

    bool receiveFile();
    bool isOpened() const;
    DataChannel(const DataChannel &) = delete;

    DataChannel &operator=(const DataChannel &) = delete;
    bool isBusy() const;
    const TransferSession &getTransferSession() const;

private:
    UDPSocket *socket;

    RDTSender *sender;

    RDTReceiver *receiver;

    FileSender *fileSender;
    FileReceiver *fileReceiver;
    bool initializeNetwork();

    void cleanupNetwork();
    void resetResources();

    bool opened;
    DataChannelConfig config;
    bool busy = false;
};

#endif