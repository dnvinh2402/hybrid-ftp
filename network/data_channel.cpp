#include "data_channel.h"

#include "../common/logger.h"
#include "../common/socket_platform.h"

#include "file_receiver.h"
#include "file_sender.h"
#include "packet_builder.h"
#include "rdt_receiver.h"
#include "rdt_sender.h"
#include "sliding_window_sender.h"
#include "udp_socket.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
std::filesystem::path makeUniqueAsciiTempPath()
{
    static std::atomic<unsigned long long> counter{0};

    const auto now =
        std::chrono::steady_clock::now().time_since_epoch().count();

    return std::filesystem::temp_directory_path()
        / ("hybridftp_ascii_"
           + std::to_string(now)
           + "_"
           + std::to_string(counter.fetch_add(1))
           + ".tmp");
}

bool createCRLFFile(
    const std::string &srcPath,
    const std::filesystem::path &destPath)
{
    std::ifstream in(srcPath, std::ios::binary);
    std::ofstream out(destPath, std::ios::binary | std::ios::trunc);

    if (!in.is_open() || !out.is_open())
    {
        return false;
    }

    char c = 0;
    char previous = 0;

    while (in.get(c))
    {
        if (c == '\n' && previous != '\r')
        {
            out.put('\r');
        }

        out.put(c);
        previous = c;
    }

    return out.good();
}
}

DataChannel::DataChannel()
    : socket(nullptr),
      sender(nullptr),
      receiver(nullptr),
      fileSender(nullptr),
      fileReceiver(nullptr),
      opened(false),
      busy(false),
      windowSender(nullptr)
{
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
        log_error("Failed to initialize socket platform.");
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
    if (opened)
    {
        log_info("DataChannel already opened.");
        return true;
    }

    this->config = config;

    if (!initializeNetwork())
    {
        return false;
    }

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

    if (!socket->setReceiveTimeout(this->config.timeout))
    {
        resetResources();
        cleanupNetwork();
        log_error("Open DataChannel failed: cannot configure timeout.");
        return false;
    }

    sender = new RDTSender(*socket, this->config.maxRetry);
    receiver = new RDTReceiver(*socket, this->config.simulateAckLoss);

    if (this->config.useGBN)
    {
        windowSender = new SlidingWindowSender(*socket, this->config.maxRetry);
    }

    fileSender = new FileSender(*sender);
    fileReceiver = new FileReceiver(*receiver);

    if (windowSender != nullptr)
    {
        fileSender->setWindowSender(windowSender);
    }

    opened = true;
    busy.store(false);

    log_info("================================");
    log_info("Open DataChannel");
    log_info("Local Port : " + std::to_string(this->config.localPort));
    log_info("Timeout    : " + std::to_string(this->config.timeout) + " ms");
    log_info(
        "Mode       : "
        + std::string(
            this->config.useGBN
                ? ("Go-Back-N (W="
                   + std::to_string(SlidingWindowSender::WINDOW_SIZE)
                   + ")")
                : "Stop-and-Wait"));
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
    {
        return;
    }

    // The owner must stop the active transfer before closing its resources.
    if (busy.load())
    {
        log_error("Refusing to close DataChannel while a transfer is active.");
        return;
    }

    log_info("Closing DataChannel.");

    resetResources();
    cleanupNetwork();

    opened = false;
    busy.store(false);
}

int DataChannel::getSocketFd() const
{
    if (opened && socket != nullptr)
    {
        return static_cast<int>(socket->getSocketFd());
    }

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

    bool expected = false;
    if (!busy.compare_exchange_strong(expected, true))
    {
        log_error("DataChannel is busy.");
        return false;
    }

    bool success = false;

    if (type == TransferType::ASCII)
    {
        const std::filesystem::path tempFile = makeUniqueAsciiTempPath();

        if (!createCRLFFile(file, tempFile))
        {
            log_error("Failed to create ASCII CRLF temporary file.");
            busy.store(false);
            return false;
        }

        success = fileSender->sendFile(tempFile.string(), ip, port);

        std::error_code ec;
        std::filesystem::remove(tempFile, ec);
    }
    else
    {
        success = fileSender->sendFile(file, ip, port);
    }

    busy.store(false);
    return success;
}

bool DataChannel::receiveFile(const std::string &outputDir)
{
    if (!opened || fileReceiver == nullptr)
    {
        log_error("DataChannel is not opened.");
        return false;
    }

    bool expected = false;
    if (!busy.compare_exchange_strong(expected, true))
    {
        log_error("DataChannel is busy.");
        return false;
    }

    const bool result = fileReceiver->receiveFile(outputDir);

    busy.store(false);
    return result;
}

bool DataChannel::isBusy() const
{
    return busy.load();
}

const TransferSession &DataChannel::getTransferSession() const
{
    return fileSender->getSession();
}

const TransferSession &DataChannel::getReceiveTransferSession() const
{
    return fileReceiver->getSession();
}

bool DataChannel::sendHandshake(
    const std::string &ip,
    unsigned short port)
{
    if (!opened || socket == nullptr)
    {
        log_error("DataChannel is not opened.");
        return false;
    }

    auto syn = PacketBuilder::buildSynPacket(0);
    return socket->sendPacket(syn, ip, port);
}

bool DataChannel::receiveHandshake(
    std::string &outIp,
    unsigned short &outPort)
{
    if (!opened || socket == nullptr)
    {
        log_error("DataChannel is not opened.");
        return false;
    }

    RDTPacket packet{};
    std::string senderIp;
    unsigned short senderPort = 0;

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

    log_info(
        "Handshake OK. Client data address: "
        + outIp
        + ":"
        + std::to_string(outPort));

    return true;
}

void DataChannel::abortTransfer()
{
    if (!opened || !busy.load())
    {
        return;
    }

    log_info("DataChannel: abort requested.");

    if (fileReceiver != nullptr)
    {
        fileReceiver->abortTransfer();
    }

    if (fileSender != nullptr)
    {
        fileSender->abortTransfer();
    }

    // Do not free sockets/resources here. The transfer thread owns the
    // active operation and will return cooperatively after observing the flag.
}