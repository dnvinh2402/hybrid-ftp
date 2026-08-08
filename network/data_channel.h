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
    // Chờ ĐÚNG 1 gói "chào hỏi" (SYN) từ client, dùng cho hướng PASV+RETR/LIST
    // -- server chỉ biết port NÓ tự bind để nhận, chưa biết địa chỉ client
    // lắng nghe ở đâu; gói SYN này giúp "học" được địa chỉ đó trước khi gửi.
    bool receiveHandshake(std::string &outIp, unsigned short &outPort);
    // Gửi 1 gói SYN "chào hỏi" tới server -- dùng ở phía CLIENT khi tải file
    // qua PASV, để server học được địa chỉ mình trước khi server gửi file.
    // Không chờ ACK (server không trả lời riêng cho gói này).
    bool sendHandshake(const std::string &ip, unsigned short port);
    bool isOpened() const;
    DataChannel(const DataChannel &) = delete;

    DataChannel &operator=(const DataChannel &) = delete;
    bool isBusy() const;
    const TransferSession &getTransferSession() const;      // session của lần GỬI gần nhất (RETR)
    const TransferSession &getReceiveTransferSession() const; // session của lần NHẬN gần nhất (STOR)

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