#include <fstream>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include "file_sender.h"
#include "sliding_window_sender.h"
#include "../common/logger.h"

using Clock = std::chrono::steady_clock;

// ----------------------------------------------------------------
#include "file_sender.h"
#include <filesystem>
FileSender::FileSender(RDTSender &sender)
    : rdtSender(sender)
{
}
void FileSender::abortTransfer()
{
    aborted.store(true);
    if (swSender != nullptr)
    {
        swSender->abortTransfer(); // Kích hoạt ngắt cho cả Go-Back-N
    }
}
void FileSender::resetSession()
{
    session = TransferSession();
}

void FileSender::setWindowSender(SlidingWindowSender *sw)
{
    swSender = sw;
}

// ----------------------------------------------------------------
//  Progress bar: [=========>   ] 75%  3.2 MB / 4.3 MB  8.4 MB/s
// ----------------------------------------------------------------
static void printProgress(uint64_t done, uint64_t total, double elapsedSec)
{
    if (total == 0)
        return;

    int pct = static_cast<int>(done * 100 / total);
    int width = 28;
    int fill = pct * width / 100;

    std::string bar(fill, '=');
    if (fill < width)
        bar += '>';
    bar += std::string(width - static_cast<int>(bar.size()), ' ');

    double mbDone = done / 1048576.0;
    double mbTotal = total / 1048576.0;
    double speed = (elapsedSec > 0.001) ? (done / 1048576.0 / elapsedSec) : 0.0;

    std::ostringstream oss;
    oss << "\r[" << bar << "] "
        << std::setw(3) << pct << "%  "
        << std::fixed << std::setprecision(1)
        << mbDone << " / " << mbTotal << " MB  "
        << std::setprecision(2) << speed << " MB/s   ";
    std::cout << oss.str() << std::flush;
}

// ----------------------------------------------------------------
bool FileSender::sendFile(const std::string &filePath,
                          const std::string &receiverIp,
                          unsigned short receiverPort)
{
    resetSession();

    session.remoteIp   = receiverIp;
    session.remotePort = receiverPort;
    session.fileName   = filePath;

    aborted.store(false);

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        log_error("Cannot open file: " + filePath);
        return false;
    }

    file.seekg(0, std::ios::end);
    uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    session.fileSize = fileSize;

    FileMetadata meta{};
    std::string filename = std::filesystem::path(filePath).filename().string();
    std::strncpy(meta.fileName, filename.c_str(), MAX_FILENAME_LENGTH - 1);
    meta.fileName[MAX_FILENAME_LENGTH - 1] = '\0';
    meta.fileSize = fileSize;

    std::string modeStr = swSender
                              ? ("Go-Back-N (W=" + std::to_string(SlidingWindowSender::WINDOW_SIZE) + ")")
                              : "Stop-and-Wait";

    log_info("--------------------------------");
    log_info("Transfer Start (" + modeStr + ")");
    log_info("File   : " + filePath);
    log_info("Size   : " + std::to_string(fileSize) + " bytes");
    log_info("Remote : " + receiverIp + ":" + std::to_string(receiverPort));
    log_info("--------------------------------");

    auto startTime = Clock::now();
    auto elapsedSec = [&]() -> double
    {
        return std::chrono::duration<double>(Clock::now() - startTime).count();
    };

    // ── 1. GỬI META PACKET (ĐÃ SỬA BUG GỬI 2 LẦN) ───────────────────
    auto metaPacket = PacketBuilder::buildMetaPacket(session.nextSeq, meta);
    bool metaSuccess = false;

    if (swSender != nullptr)
    {
        swSender->beginSession(receiverIp, receiverPort);
        metaSuccess = swSender->send(metaPacket) && swSender->flush();
    }
    else
    {
        metaSuccess = rdtSender.send(metaPacket, receiverIp, receiverPort);
    }

    if (!metaSuccess)
    {
        if (aborted.load())
            log_info("FileSender: transfer aborted during META send.");
        else
            log_error("Failed to send metadata at seq=" + std::to_string(session.nextSeq));

        file.close();
        return false;
    }

    log_info("Metadata sent. Name=" + std::string(meta.fileName) +
             " Size=" + std::to_string(meta.fileSize));
    session.nextSeq++;

    // ── 2. GỬI DATA PACKETS ─────────────────────────────────────────
    char buffer[MAX_PAYLOAD_SIZE];

    while (true)
    {
        if (aborted.load())
        {
            log_info("FileSender: aborted by ABOR command.");
            if (file.is_open()) file.close();
            return false;
        }

        file.read(buffer, MAX_PAYLOAD_SIZE);

        if (file.bad())
        {
            log_error("Read file failed.");
            file.close();
            return false;
        }

        std::streamsize bytesRead = file.gcount();
        if (bytesRead <= 0) break;

        auto packet = PacketBuilder::buildDataPacket(
            session.nextSeq,
            buffer,
            static_cast<uint16_t>(bytesRead));

        bool dataSuccess = false;
        if (swSender != nullptr)
        {
            dataSuccess = swSender->send(packet);
        }
        else
        {
            dataSuccess = rdtSender.send(packet, receiverIp, receiverPort);
        }

        if (!dataSuccess)
        {
            if (aborted.load())
                log_info("FileSender: transfer aborted during UDP send.");
            else
                log_error("FileSender: UDP send failed at seq=" + std::to_string(session.nextSeq));

            if (file.is_open()) file.close();
            return false;
        }

        session.nextSeq++;
        session.bytesTransferred += static_cast<uint64_t>(bytesRead);
        session.packetsTransferred++;

        if (session.packetsTransferred % 16 == 0 || file.eof())
            printProgress(session.bytesTransferred, fileSize, elapsedSec());
    }

    file.close();
    std::cout << std::endl;

    // ── 3. GỬI FIN PACKET ───────────────────────────────────────────
    auto finPacket = PacketBuilder::buildFinPacket(session.nextSeq);
    bool finSuccess = false;

    if (swSender != nullptr)
    {
        finSuccess = swSender->send(finPacket) && swSender->flush();
    }
    else
    {
        finSuccess = rdtSender.send(finPacket, receiverIp, receiverPort);
    }

    if (!finSuccess)
    {
        if (aborted.load())
            log_info("FileSender: transfer aborted during FIN send.");
        else
            log_error("Failed to send/flush FIN.");

        return false;
    }

    log_info("FIN sent and ACKed.");
    session.finished = true;

    printSummary(elapsedSec());
    return true;
}

// ----------------------------------------------------------------
void FileSender::printSummary(double elapsedSec) const
{
    double speedMBs = (elapsedSec > 0.001)
                          ? (session.bytesTransferred / 1048576.0 / elapsedSec)
                          : 0.0;

    std::ostringstream timeStr, speedStr;
    timeStr << std::fixed << std::setprecision(2) << elapsedSec << " s";
    speedStr << std::fixed << std::setprecision(2) << speedMBs << " MB/s";

    std::string modeStr = swSender
                              ? ("Go-Back-N (W=" + std::to_string(SlidingWindowSender::WINDOW_SIZE) + ")")
                              : "Stop-and-Wait";

    log_info("--------------------------------");
    log_info("Transfer Summary");
    log_info("File     : " + session.fileName);
    log_info("Packets  : " + std::to_string(session.packetsTransferred));
    log_info("Bytes    : " + std::to_string(session.bytesTransferred));
    log_info("Size     : " + std::to_string(session.fileSize));
    log_info("Time     : " + timeStr.str());
    log_info("Speed    : " + speedStr.str());
    log_info("Mode     : " + modeStr);
    log_info("Finished : " + std::string(session.finished ? "Yes" : "No"));
    log_info("--------------------------------");
}

// ----------------------------------------------------------------
const TransferSession &FileSender::getSession() const
{
    return session;
}