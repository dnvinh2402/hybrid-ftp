#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

#include "../network/data_channel.h"
#include "../network/data_channel_config.h"

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

std::string readAll(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    assert(in.is_open());

    return std::string(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

void writeAll(
    const fs::path &path,
    const std::string &content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out.is_open());
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    assert(out.good());
}

fs::path transferOnce(
    const fs::path &sourcePath,
    const fs::path &receiveDir,
    TransferType type,
    unsigned short firstPort)
{
    DataChannel receiver;
    DataChannelConfig receiverConfig;
    receiverConfig.timeout = 3000;
    receiverConfig.maxRetry = 5;
    receiverConfig.useGBN = true;

    const unsigned short receiverPort =
        openReceiver(receiver, receiverConfig, firstPort);

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
    senderConfig.useGBN = true;

    assert(sender.open(senderConfig));

    const bool sendOk = sender.sendFile(
        sourcePath.string(),
        "127.0.0.1",
        receiverPort,
        type);

    receiverThread.join();

    assert(sendOk);
    assert(receiveOk);

    const TransferSession &received = receiver.getReceiveTransferSession();
    const fs::path receivedPath = receiveDir / received.fileName;

    assert(fs::exists(receivedPath));

    sender.close();
    receiver.close();

    return receivedPath;
}
}

int main()
{
    const fs::path testRoot =
        fs::temp_directory_path() / "hybridftp_ascii_transfer";
    const fs::path asciiReceiveDir = testRoot / "ascii_received";
    const fs::path binaryReceiveDir = testRoot / "binary_received";
    const fs::path sourcePath = testRoot / "mixed_line_endings.txt";

    std::error_code ec;
    fs::remove_all(testRoot, ec);
    ec.clear();
    fs::create_directories(asciiReceiveDir, ec);
    assert(!ec);
    fs::create_directories(binaryReceiveDir, ec);
    assert(!ec);

    const std::string sourceContent =
        "line-1\n"
        "line-2\r\n"
        "line-3\n";

    const std::string expectedNetworkAscii =
        "line-1\r\n"
        "line-2\r\n"
        "line-3\r\n";

    writeAll(sourcePath, sourceContent);

    const fs::path asciiReceived = transferOnce(
        sourcePath,
        asciiReceiveDir,
        TransferType::ASCII,
        47300);

    const fs::path binaryReceived = transferOnce(
        sourcePath,
        binaryReceiveDir,
        TransferType::BINARY,
        47330);

    // TYPE A normalizes lone LF line endings to CRLF on the data channel.
    assert(readAll(asciiReceived) == expectedNetworkAscii);

    // TYPE I must preserve every byte exactly.
    assert(readAll(binaryReceived) == sourceContent);

    fs::remove_all(testRoot, ec);

    std::cout
        << "ascii_transfer_tests passed: "
           "ASCII uses CRLF while binary preserves exact bytes\n";

    return 0;
}