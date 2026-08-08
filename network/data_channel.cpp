#include "data_channel.h"
#include "../common/logger.h"
#include "udp_socket.h"
#include "rdt_sender.h"
#include "rdt_receiver.h"
#include "file_sender.h"
#include "file_receiver.h"
#include "packet_builder.h"

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif
DataChannel::DataChannel()
{
    socket = nullptr;
    sender = nullptr;
    receiver = nullptr;
    fileSender = nullptr;
    fileReceiver = nullptr;
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

    if (socket != nullptr)
    {
        socket->close();
        delete socket;
        socket = nullptr;
    }
}
bool DataChannel::initializeNetwork()
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        log_error("WSAStartup failed.");
        return false;
    }
    log_info("Winsock initialized.");
#endif
    return true;
}
void DataChannel::cleanupNetwork()
{
#ifdef _WIN32
    WSACleanup();
    log_info("Winsock cleaned up.");
#endif
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

    fileSender = new FileSender(*sender);
    fileReceiver = new FileReceiver(*receiver);

    opened = true;
    log_info("--------------------------------");
    log_info("Open DataChannel");
    log_info("Local Port : " + std::to_string(this->config.localPort));
    log_info("Timeout : " + std::to_string(this->config.timeout) + " ms");
    log_info("--------------------------------");

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

    log_info("--------------------------------");
    log_info("Closing DataChannel");
    log_info("Local Port : " + std::to_string(config.localPort));
    resetResources();
    cleanupNetwork();
    busy = false;
    opened = false;
    log_info("--------------------------------");
}
bool DataChannel::sendFile(
    const std::string &file,
    const std::string &ip,
    unsigned short port)
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
    bool success = fileSender->sendFile(file, ip, port);
    busy = false;
    return success;
}
bool DataChannel::receiveFile()
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
    bool result = fileReceiver->receiveFile();
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