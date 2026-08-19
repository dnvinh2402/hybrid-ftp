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
    // outputDir: thư mục TẠM để ghi file trong lúc nhận -- mặc định
    // "server_files" (giữ tương thích ngược). Server nên truyền vào 1
    // thư mục RIÊNG THEO SESSION (vd "server_files/session_5") để 2
    // client nhận file CÙNG TÊN cùng lúc không ghi đè/lẫn dữ liệu vào
    // nhau -- đây chính là bug isolation đã phát hiện qua test.
    bool receiveFile(const std::string& outputDir = "server_files");
    const TransferSession& getSession() const;
};

#endif