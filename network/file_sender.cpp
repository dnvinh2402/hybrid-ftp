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
FileSender::FileSender(RDTSender &sender)
    : rdtSender(sender)
{
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
    if (total == 0) return;

    int pct   = static_cast<int>(done * 100 / total);
    int width = 28;
    int fill  = pct * width / 100;

    std::string bar(fill, '=');
    if (fill < width) bar += '>';
    bar += std::string(width - static_cast<int>(bar.size()), ' ');

    double mbDone  = done  / 1048576.0;
    double mbTotal = total / 1048576.0;
    double speed   = (elapsedSec > 0.001) ? (done / 1048576.0 / elapsedSec) : 0.0;

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

    // Mở file
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

    // Xây metadata
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
    auto elapsedSec = [&]() -> double {
        return std::chrono::duration<double>(Clock::now() - startTime).count();
    };

    // Gửi META
    auto metaPacket = PacketBuilder::buildMetaPacket(session.nextSeq, meta);

    if (swSender != nullptr)
    {
        swSender->beginSession(receiverIp, receiverPort);
        if (!swSender->send(metaPacket))
        {
            log_error("Failed to send metadata (GBN).");
            file.close();
            return false;
        }
        if (!swSender->flush())
        {
            log_error("Failed to flush metadata ACK (GBN).");
            file.close();
            return false;
        }
    }
    else
    {
        if (!rdtSender.send(metaPacket, receiverIp, receiverPort))
        {
            log_error("Failed to send metadata (S&W).");
            file.close();
            return false;
        }
    }

    log_info("Metadata sent. Name=" + std::string(meta.fileName) +
             " Size=" + std::to_string(meta.fileSize));
    session.nextSeq++;

    // Gửi DATA packets
    char buffer[MAX_PAYLOAD_SIZE];

    while (true)
    {
        file.read(buffer, MAX_PAYLOAD_SIZE);

        if (file.bad())
        {
            log_error("Read file failed.");
            file.close();
            return false;
        }

        std::streamsize bytesRead = file.gcount();
        if (bytesRead <= 0)
            break;

        auto packet = PacketBuilder::buildDataPacket(
            session.nextSeq,
            buffer,
            static_cast<uint16_t>(bytesRead));

        if (swSender != nullptr)
        {
            if (!swSender->send(packet))
            {
                log_error("GBN send failed at seq=" + std::to_string(session.nextSeq));
                file.close();
                return false;
            }
        }
        else
        {
            if (!rdtSender.send(packet, receiverIp, receiverPort))
            {
                log_error("S&W send failed at seq=" + std::to_string(session.nextSeq));
                file.close();
                return false;
            }
        }

        session.nextSeq++;
        session.bytesTransferred  += static_cast<uint64_t>(bytesRead);
        session.packetsTransferred++;

        // Cập nhật progress mỗi 16 packet (~22 KB)
        if (session.packetsTransferred % 16 == 0 || file.eof())
            printProgress(session.bytesTransferred, fileSize, elapsedSec());
    }

    file.close();
    std::cout << std::endl;

    // Gửi FIN
    auto finPacket = PacketBuilder::buildFinPacket(session.nextSeq);

    if (swSender != nullptr)
    {
        if (!swSender->send(finPacket))
        {
            log_error("Failed to send FIN (GBN).");
            return false;
        }
        if (!swSender->flush())
        {
            log_error("Failed to flush remaining ACKs after FIN.");
            return false;
        }
    }
    else
    {
        if (!rdtSender.send(finPacket, receiverIp, receiverPort))
        {
            log_error("Failed to send FIN (S&W).");
            return false;
        }
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
    timeStr  << std::fixed << std::setprecision(2) << elapsedSec << " s";
    speedStr << std::fixed << std::setprecision(2) << speedMBs   << " MB/s";

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