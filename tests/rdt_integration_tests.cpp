#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "../common/sha256.h"
#include "../network/data_channel.h"
#include "../network/data_channel_config.h"

namespace fs = std::filesystem;

int main()
{
    const fs::path testRoot =
        fs::temp_directory_path() / "hybridftp_rdt_integration";
    const fs::path receiveDir = testRoot / "received";
    const fs::path sourcePath = testRoot / "ack_loss_test.bin";

    std::error_code ec;
    fs::remove_all(testRoot, ec);
    fs::create_directories(receiveDir, ec);
    assert(!ec);

    {
        std::ofstream out(sourcePath, std::ios::binary | std::ios::trunc);
        assert(out.is_open());

        for (int i = 0; i < 65536; ++i)
        {
            const char value = static_cast<char>(i % 251);
            out.write(&value, 1);
        }
    }

    DataChannel receiver;
    DataChannelConfig receiverConfig;
    receiverConfig.timeout = 5000;
    receiverConfig.maxRetry = 5;
    receiverConfig.useGBN = true;
    receiverConfig.simulateAckLoss = true;

    unsigned short receiverPort = 0;
    for (unsigned short candidate = 46123; candidate < 46133; ++candidate)
    {
        receiverConfig.localPort = candidate;
        if (receiver.open(receiverConfig))
        {
            receiverPort = candidate;
            break;
        }
    }

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
    senderConfig.timeout = 5000;
    senderConfig.maxRetry = 5;
    senderConfig.useGBN = true;

    assert(sender.open(senderConfig));

    const bool sendOk = sender.sendFile(
        sourcePath.string(),
        "127.0.0.1",
        receiverPort,
        TransferType::BINARY);

    receiverThread.join();

    assert(sendOk);
    assert(receiveOk);

    const TransferSession &received = receiver.getReceiveTransferSession();
    const fs::path receivedPath = receiveDir / received.fileName;

    assert(fs::exists(receivedPath));
    assert(fs::file_size(receivedPath) == fs::file_size(sourcePath));
    assert(SHA256::hashFile(sourcePath.string()) ==
           SHA256::hashFile(receivedPath.string()));

    sender.close();
    receiver.close();

    fs::remove_all(testRoot, ec);

    std::cout
        << "rdt_integration_tests passed: GBN recovered from an injected ACK loss\n";

    return 0;
}