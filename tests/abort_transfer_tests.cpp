#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "../common/file_metadata.h"
#include "../network/data_channel.h"
#include "../network/data_channel_config.h"
#include "../network/packet_builder.h"
#include "../network/udp_socket.h"

namespace fs = std::filesystem;

namespace
{
unsigned short openReceiver(
    DataChannel &receiver,
    DataChannelConfig &config,
    unsigned short firstPort)
{
    for (unsigned short port = firstPort; port < firstPort + 20; ++port)
    {
        config.localPort = port;

        if (receiver.open(config))
        {
            return port;
        }
    }

    return 0;
}

void writeFixture(
    const fs::path &path,
    std::size_t size)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out.is_open());

    for (std::size_t i = 0; i < size; ++i)
    {
        const char value = static_cast<char>(i % 251U);
        out.write(&value, 1);
    }

    assert(out.good());
}

void testSenderAbort(const fs::path &testRoot)
{
    const fs::path sourcePath = testRoot / "sender_abort.bin";
    writeFixture(sourcePath, 64U * 1024U);

    DataChannel sender;
    DataChannelConfig config;
    config.localPort = 0;
    config.timeout = 500;
    config.maxRetry = 20;
    config.useGBN = true;

    assert(sender.open(config));

    bool sendResult = true;

    std::thread senderThread(
        [&]()
        {
            // No receiver is intentionally listening on this destination.
            // The sender remains inside the reliable-transfer loop until ABOR.
            sendResult = sender.sendFile(
                sourcePath.string(),
                "127.0.0.1",
                48991,
                TransferType::BINARY);
        });

    for (int i = 0; i < 50 && !sender.isBusy(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(sender.isBusy());

    sender.abortTransfer();
    senderThread.join();

    assert(!sendResult);
    assert(!sender.isBusy());

    sender.close();
}

void testReceiverAbortRemovesPartialFile(const fs::path &testRoot)
{
    const fs::path receiveDir = testRoot / "receiver_abort";
    fs::create_directories(receiveDir);

    DataChannel receiver;
    DataChannelConfig receiverConfig;
    receiverConfig.timeout = 100;
    receiverConfig.maxRetry = 5;
    receiverConfig.useGBN = true;

    const unsigned short receiverPort =
        openReceiver(receiver, receiverConfig, 47400);

    assert(receiverPort != 0);

    bool receiveResult = true;

    std::thread receiverThread(
        [&]()
        {
            receiveResult = receiver.receiveFile(receiveDir.string());
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    UDPSocket rawSender;
    assert(rawSender.create());
    assert(rawSender.bind(0));

    FileMetadata metadata{};
    const std::string fileName = "partial_abort.bin";
    std::strncpy(
        metadata.fileName,
        fileName.c_str(),
        MAX_FILENAME_LENGTH - 1);
    metadata.fileName[MAX_FILENAME_LENGTH - 1] = '\0';
    metadata.fileSize = 8192;

    const RDTPacket metaPacket =
        PacketBuilder::buildMetaPacket(0, metadata);

    assert(rawSender.sendPacket(metaPacket, "127.0.0.1", receiverPort));

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    const char payload[] = "partial-data";
    const RDTPacket dataPacket = PacketBuilder::buildDataPacket(
        1,
        payload,
        static_cast<std::uint16_t>(sizeof(payload) - 1));

    assert(rawSender.sendPacket(dataPacket, "127.0.0.1", receiverPort));

    const fs::path partialPath = receiveDir / fileName;

    for (int i = 0; i < 50 && !fs::exists(partialPath); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(fs::exists(partialPath));

    receiver.abortTransfer();
    receiverThread.join();

    assert(!receiveResult);
    assert(!receiver.isBusy());
    assert(!fs::exists(partialPath));

    rawSender.close();
    receiver.close();
}
}

int main()
{
    const fs::path testRoot =
        fs::temp_directory_path() / "hybridftp_abort_transfer";

    std::error_code ec;
    fs::remove_all(testRoot, ec);
    ec.clear();
    fs::create_directories(testRoot, ec);
    assert(!ec);

    testSenderAbort(testRoot);
    testReceiverAbortRemovesPartialFile(testRoot);

    fs::remove_all(testRoot, ec);

    std::cout
        << "abort_transfer_tests passed: "
           "sender cancellation and partial-file cleanup verified\n";

    return 0;
}