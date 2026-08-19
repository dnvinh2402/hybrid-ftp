#include "data_channel.h"

#include "../common/logger.h"
#include "../common/protocol.h"
#include "../common/socket_platform.h"

#include "udp_socket.h"
#include "rdt_sender.h"
#include "rdt_receiver.h"
#include "file_sender.h"
#include "file_receiver.h"
#include "packet_builder.h"

#include <cstdio>
#include <fstream>

DataChannel::DataChannel()
{
    socket = nullptr;
    sender = nullptr;
    receiver = nullptr;
    fileSender = nullptr;
    fileReceiver = nullptr;
    windowSender = nullptr;
    opened = false;
}

DataChannel::~DataChannel()
{
    close();
}

void DataChannel::resetResources()
{
    delete fileSender;
    fileSender = nullptr;

    delete fileReceiver;
    fileReceiver = nullptr;

    delete sender;
    sender = nullptr;

    delete receiver;
    receiver = nullptr;

    delete windowSender;
    windowSender = nullptr;

    if (socket != nullptr)
    {
        socket->close();
        delete socket;
        socket = nullptr;
    }
}
bool DataChannel::initializeNetwork()
{
    if (!SocketPlatform::initialize())
    {
        log_error(
            "Failed to initialize "
            "socket platform.");

        return false;
    }

    return true;
}
void DataChannel::cleanupNetwork()
{
    SocketPlatform::cleanup();
}
bool DataChannel::open(const DataChannelConfig &config)
{
    this->config = config;
    if (opened)
    {
        log_info("DataChannel already opened.");
        return true;
    }

    if (!initializeNetwork())
        return false;

    socket = new UDPSocket();

    if (!socket->create())
    {
        resetResources();
        cleanupNetwork();
        log_error("Open DataChannel failed.");
        return false;
    }

    if (!socket->bind(this->config.localPort))
    {
        resetResources();
        cleanupNetwork();
        log_error("Open DataChannel failed.");
        return false;
    }

    socket->setReceiveTimeout(this->config.timeout);

    sender = new RDTSender(*socket);

    // Truyền config.simulateAckLoss vào RDTReceiver -- trước đây RDTReceiver
    // tự bật hành vi test cứng bên trong nó, giờ do DataChannelConfig
    // quyết định (mặc định false -> hành vi ACK bình thường).
    receiver = new RDTReceiver(*socket, this->config.simulateAckLoss);

    if (this->config.useGBN)
    {
        windowSender = new SlidingWindowSender(*socket);
        // SlidingWindowSender.WINDOW_SIZE override bằng config nếu muốn
    }

    fileSender = new FileSender(*sender);
    fileReceiver = new FileReceiver(*receiver);

    if (windowSender != nullptr)
        fileSender->setWindowSender(windowSender);
    log_info("Mode     : " + std::string(this->config.useGBN
                                             ? "Go-Back-N (W=32)"
                                             : "Stop-and-Wait"));
    opened = true;
    log_info("================================");
    log_info("Open DataChannel");
    log_info("Local Port : " + std::to_string(this->config.localPort));
    log_info("Timeout : " + std::to_string(this->config.timeout) + " ms");
    log_info("================================");

    return true;
}
bool DataChannel::isOpened() const
{
    return opened;
}
void DataChannel::close()
{
    if (!opened)
        return;

    log_info("================================");
    log_info("Closing DataChannel");
    log_info("Local Port : " + std::to_string(config.localPort));
    resetResources();
    cleanupNetwork();
    busy = false;
    opened = false;
    log_info("================================");
}

static bool createCRLFFile(const std::string &srcPath, const std::string &destPath)
{
    std::ifstream in(srcPath, std::ios::binary);
    std::ofstream out(destPath, std::ios::binary);
    if (!in.is_open() || !out.is_open()) return false;
    
    char c;
    char prev = 0;
    while (in.get(c)) {
        if (c =='\n' && prev != '\r') {
            out.put('\r'); // Tự động chèn \r trước \n nếu chưa có
        }
        out.put(c);
        prev = c;
    }
    return true;
}
int DataChannel::getSocketFd() const
{
    // Kiểm tra xem kênh đã mở và con trỏ socket đã được khởi tạo chưa
    if (opened && socket != nullptr)
    {
        // Gọi hàm getSocketFd() của đối tượng UDPSocket
        return static_cast<int>(socket->getSocketFd());
    }
    
    // Nếu chưa mở kênh hoặc con trỏ null, trả về -1 theo đúng comment trong file header
    return -1;
}
bool DataChannel::sendFile(
    const std::string &file,
    const std::string &ip,
    unsigned short port,
    TransferType type)
{
    if (!opened || fileSender == nullptr)
    {
        log_error("DataChannel is not opened.");
        return false;
    }

    if (busy)
    {
        log_error("DataChannel is busy.");
        return false;
    }

    busy = true;
    bool success = false;

    if (type == TransferType::ASCII) {
        // Tạo file tạm với CRLF
        std::string tempFile = file + ".tmp_ascii";
        if (!createCRLFFile(file, tempFile)) {
            log_error("Failed to create CRLF temporary file.");
            busy = false;
            return false;
        }
        //gửi file tạm bằng fileSender hiện có
        success = fileSender->sendFile(tempFile, ip, port);

        std::remove(tempFile.c_str()); // Xóa file tạm sau khi gửi
    } else {

        //BINARY transfer, gửi trực tiếp file gốc
        success = fileSender->sendFile(file, ip, port);
    }
    busy = false;
    return success;
}
bool DataChannel::receiveFile(const std::string& outputDir)
{
    if (!opened || fileReceiver == nullptr)
    {
        log_error("DataChannel is not opened.");
        return false;
    }
    if (busy)
    {
        log_error("DataChannel is busy.");
        return false;
    }

    busy = true;
    bool result = fileReceiver->receiveFile(outputDir);
    busy = false;
    return result;
}
bool DataChannel::isBusy() const
{
    return busy;
}
const TransferSession &DataChannel::getTransferSession() const
{
    return fileSender->getSession();
}
const TransferSession &DataChannel::getReceiveTransferSession() const
{
    return fileReceiver->getSession();
}
bool DataChannel::sendHandshake(const std::string &ip, unsigned short port)
{
    if (!opened || socket == nullptr)
    {
        log_error("DataChannel is not opened.");
        return false;
    }
    auto syn = PacketBuilder::buildSynPacket(0);
    return socket->sendPacket(syn, ip, port);
}
bool DataChannel::receiveHandshake(std::string &outIp, unsigned short &outPort)
{
    if (!opened || socket == nullptr)
    {
        log_error("DataChannel is not opened.");
        return false;
    }

    RDTPacket packet;
    std::string senderIp;
    unsigned short senderPort;

    // Đọc trực tiếp qua socket (không qua RDTReceiver::receive(), vì gói
    // SYN không cần ACK lại) -- timeout đã được set khi open() (setReceiveTimeout).
    if (!socket->receivePacket(packet, senderIp, senderPort))
    {
        log_error("Handshake timeout: no SYN received from client.");
        return false;
    }

    if (!verifyChecksum(packet) || !(packet.header.flags & RDTFlag::SYN))
    {
        log_error("Handshake failed: invalid or non-SYN packet.");
        return false;
    }

    outIp = senderIp;
    outPort = senderPort;
    log_info("Handshake OK. Client data address: " + outIp + ":" + std::to_string(outPort));
    return true;
}
void DataChannel::abortTransfer()
{
    if (!opened)
        return;

    log_info("DataChannel: Initiating abort sequence...");

    if (fileReceiver != nullptr)
    {
        fileReceiver->abortTransfer();
    }

    if (fileSender != nullptr)
    {
        fileSender->abortTransfer();
    }

    // KHÔNG close() ở đây.
    // Transfer hiện tại phải tự thoát trước.
}