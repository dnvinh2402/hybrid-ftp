#ifndef DATA_CHANNEL_H
#define DATA_CHANNEL_H

#include <string>
#include "data_channel_config.h"
#include "transfer_session.h"
#include "../common/protocol.h"
#include "sliding_window_sender.h" 
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
                  unsigned short port,
                TransferType type = TransferType::BINARY);

    // outputDir: xem giải thích trong FileReceiver::receiveFile() --
    // truyền thư mục RIÊNG THEO SESSION để tránh 2 client ghi đè lẫn nhau.
    bool receiveFile(const std::string& outputDir = "server_files");
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
    
    // Trả về số hiệu socket UDP thật (native OS fd) đang dùng, -1 nếu
    // chưa mở kênh. Dùng để "chứng minh" mỗi session sở hữu 1 tài nguyên
    // OS hoàn toàn riêng biệt -- phục vụ mục đích giám sát/isolation,
    // không ảnh hưởng logic gửi/nhận.
    int getSocketFd() const;

    void abortTransfer();

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
    SlidingWindowSender* windowSender = nullptr;
};

#endif