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

void writePattern(
    const fs::path &path,
    char prefix,
    std::size_t size)
{
    fs::create_directories(path.parent_path());

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out.is_open());

    for (std::size_t i = 0; i < size; ++i)
    {
        const char value = static_cast<char>(
            static_cast<unsigned char>(prefix)
            + static_cast<unsigned char>(i % 7U));
        out.write(&value, 1);
    }

    assert(out.good());
}
}

int main()
{
    const fs::path testRoot =
        fs::temp_directory_path() / "hybridftp_session_isolation";

    const fs::path sourceA = testRoot / "source_a" / "same.bin";
    const fs::path sourceB = testRoot / "source_b" / "same.bin";
    const fs::path receiveA = testRoot / "session_101";
    const fs::path receiveB = testRoot / "session_202";

    std::error_code ec;
    fs::remove_all(testRoot, ec);
    ec.clear();
    fs::create_directories(receiveA, ec);
    assert(!ec);
    fs::create_directories(receiveB, ec);
    assert(!ec);

    writePattern(sourceA, 'A', 192U * 1024U);
    writePattern(sourceB, 'K', 192U * 1024U);

    assert(SHA256::hashFile(sourceA.string()) !=
           SHA256::hashFile(sourceB.string()));

    DataChannel receiverA;
    DataChannel receiverB;

    DataChannelConfig receiverConfigA;
    receiverConfigA.timeout = 3000;
    receiverConfigA.maxRetry = 5;
    receiverConfigA.useGBN = true;

    DataChannelConfig receiverConfigB = receiverConfigA;

    const unsigned short portA =
        openReceiver(receiverA, receiverConfigA, 47500);
    const unsigned short portB =
        openReceiver(receiverB, receiverConfigB, 47530);

    assert(portA != 0);
    assert(portB != 0);
    assert(portA != portB);
    assert(receiverA.getSocketFd() != -1);
    assert(receiverB.getSocketFd() != -1);
    assert(receiverA.getSocketFd() != receiverB.getSocketFd());

    bool receiveOkA = false;
    bool receiveOkB = false;

    std::thread receiverThreadA(
        [&]()
        {
            receiveOkA = receiverA.receiveFile(receiveA.string());
        });

    std::thread receiverThreadB(
        [&]()
        {
            receiveOkB = receiverB.receiveFile(receiveB.string());
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    DataChannel senderA;
    DataChannel senderB;

    DataChannelConfig senderConfig;
    senderConfig.localPort = 0;
    senderConfig.timeout = 3000;
    senderConfig.maxRetry = 5;
    senderConfig.useGBN = true;

    assert(senderA.open(senderConfig));
    assert(senderB.open(senderConfig));
    assert(senderA.getSocketFd() != -1);
    assert(senderB.getSocketFd() != -1);
    assert(senderA.getSocketFd() != senderB.getSocketFd());

    bool sendOkA = false;
    bool sendOkB = false;

    std::thread senderThreadA(
        [&]()
        {
            sendOkA = senderA.sendFile(
                sourceA.string(),
                "127.0.0.1",
                portA,
                TransferType::BINARY);
        });

    std::thread senderThreadB(
        [&]()
        {
            sendOkB = senderB.sendFile(
                sourceB.string(),
                "127.0.0.1",
                portB,
                TransferType::BINARY);
        });

    senderThreadA.join();
    senderThreadB.join();
    receiverThreadA.join();
    receiverThreadB.join();

    assert(sendOkA);
    assert(sendOkB);
    assert(receiveOkA);
    assert(receiveOkB);

    const fs::path resultA =
        receiveA / receiverA.getReceiveTransferSession().fileName;
    const fs::path resultB =
        receiveB / receiverB.getReceiveTransferSession().fileName;

    assert(resultA.filename() == "same.bin");
    assert(resultB.filename() == "same.bin");
    assert(fs::exists(resultA));
    assert(fs::exists(resultB));

    assert(SHA256::hashFile(resultA.string()) ==
           SHA256::hashFile(sourceA.string()));
    assert(SHA256::hashFile(resultB.string()) ==
           SHA256::hashFile(sourceB.string()));
    assert(SHA256::hashFile(resultA.string()) !=
           SHA256::hashFile(resultB.string()));

    senderA.close();
    senderB.close();
    receiverA.close();
    receiverB.close();

    fs::remove_all(testRoot, ec);

    std::cout
        << "session_isolation_tests passed: "
           "concurrent same-name transfers remain isolated\n";

    return 0;
}