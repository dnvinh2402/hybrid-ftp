#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "../common/sha256.h"
#include "../network/data_channel.h"
#include "../network/data_channel_config.h"

namespace fs = std::filesystem;

namespace
{
unsigned short openReceiver(
    DataChannel &receiver,
    DataChannelConfig &config,
    unsigned short firstPort,
    unsigned short lastPort)
{
    for (unsigned short port = firstPort; port <= lastPort; ++port)
    {
        config.localPort = port;

        if (receiver.open(config))
        {
            return port;
        }
    }

    return 0;
}

void writeBinaryFixture(
    const fs::path &path,
    std::size_t size,
    unsigned int seed)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out.is_open());

    std::uint32_t value = seed;

    for (std::size_t i = 0; i < size; ++i)
    {
        value = value * 1664525U + 1013904223U;
        const char byte = static_cast<char>((value >> 24U) & 0xFFU);
        out.write(&byte, 1);
    }

    assert(out.good());
}

void runTransferCase(
    const fs::path &testRoot,
    bool useGBN,
    unsigned short firstPort)
{
    const std::string modeName = useGBN ? "gbn" : "stop_and_wait";
    const fs::path sourcePath = testRoot / (modeName + "_source.bin");
    const fs::path receiveDir = testRoot / (modeName + "_received");

    fs::create_directories(receiveDir);
    writeBinaryFixture(sourcePath, 128U * 1024U, useGBN ? 17U : 29U);

    DataChannel receiver;
    DataChannelConfig receiverConfig;
    receiverConfig.timeout = 3000;
    receiverConfig.maxRetry = 5;
    receiverConfig.useGBN = useGBN;
    receiverConfig.simulateAckLoss = false;

    const unsigned short receiverPort =
        openReceiver(receiver, receiverConfig, firstPort, firstPort + 19);

    assert(receiverPort != 0);

    bool receiveOk = false;

    std::thread receiverThread(
        [&]()
        {
            receiveOk = receiver.receiveFile(receiveDir.string());
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    DataChannel sender;
    DataChannelConfig senderConfig;
    senderConfig.localPort = 0;
    senderConfig.timeout = 3000;
    senderConfig.maxRetry = 5;
    senderConfig.useGBN = useGBN;
    senderConfig.simulateAckLoss = false;

    assert(sender.open(senderConfig));

    const bool sendOk = sender.sendFile(
        sourcePath.string(),
        "127.0.0.1",
        receiverPort,
        TransferType::BINARY);

    receiverThread.join();

    assert(sendOk);
    assert(receiveOk);

    const TransferSession &sendSession = sender.getTransferSession();
    const TransferSession &receiveSession = receiver.getReceiveTransferSession();

    assert(sendSession.finished);
    assert(receiveSession.finished);
    assert(sendSession.bytesTransferred == fs::file_size(sourcePath));
    assert(receiveSession.bytesTransferred == fs::file_size(sourcePath));
    assert(sendSession.packetsTransferred > 0);
    assert(receiveSession.packetsTransferred > 0);

    const fs::path receivedPath = receiveDir / receiveSession.fileName;

    assert(fs::exists(receivedPath));
    assert(fs::file_size(receivedPath) == fs::file_size(sourcePath));
    assert(SHA256::hashFile(sourcePath.string()) ==
           SHA256::hashFile(receivedPath.string()));

    sender.close();
    receiver.close();
}
}

int main()
{
    const fs::path testRoot =
        fs::temp_directory_path() / "hybridftp_transfer_integration";

    std::error_code ec;
    fs::remove_all(testRoot, ec);
    ec.clear();
    fs::create_directories(testRoot, ec);
    assert(!ec);

    runTransferCase(testRoot, false, 47100);
    runTransferCase(testRoot, true, 47130);

    fs::remove_all(testRoot, ec);

    std::cout
        << "transfer_integration_tests passed: "
           "Stop-and-Wait and Go-Back-N preserve binary files\n";

    return 0;
}