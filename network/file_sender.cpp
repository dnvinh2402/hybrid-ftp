#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "file_sender.h"
#include "sliding_window_sender.h"

#include "../common/logger.h"

using Clock =
    std::chrono::steady_clock;

namespace
{

// ============================================================
// Visual progress bar from the Sliding Window implementation.
// ============================================================

void printProgress(
    std::uint64_t done,
    std::uint64_t total,
    double elapsedSec)
{
    constexpr int width =
        28;

    const int percent =
        (total == 0)
            ? 100
            : static_cast<int>(
                  (done * 100ULL)
                  / total);

    const int fill =
        percent * width / 100;

    std::string bar(
        static_cast<
            std::size_t>(fill),
        '=');

    if (fill < width)
    {
        bar += '>';
    }

    if (static_cast<int>(
            bar.size()) < width)
    {
        bar += std::string(
            static_cast<
                std::size_t>(
                width
                - static_cast<int>(
                    bar.size())),
            ' ');
    }

    const double mbDone =
        done / 1048576.0;

    const double mbTotal =
        total / 1048576.0;

    const double speed =
        (elapsedSec > 0.001)
            ? (done
               / 1048576.0
               / elapsedSec)
            : 0.0;

    std::ostringstream output;

    output
        << '\r'
        << '['
        << bar
        << "] "
        << std::setw(3)
        << percent
        << "%  "
        << std::fixed
        << std::setprecision(1)
        << mbDone
        << " / "
        << mbTotal
        << " MB  "
        << std::setprecision(2)
        << speed
        << " MB/s   ";

    std::cout
        << output.str()
        << std::flush;
}

void printSendProgress(
    std::uint64_t bytesTransferred,
    std::uint64_t totalBytes,
    double elapsedSec,
    int &nextProgressMark)
{
    const int percent =
        (totalBytes == 0)
            ? 100
            : static_cast<int>(
                  (bytesTransferred
                   * 100ULL)
                  / totalBytes);

    if (percent >= nextProgressMark ||
        percent == 100)
    {
        printProgress(
            bytesTransferred,
            totalBytes,
            elapsedSec);

        while (nextProgressMark <=
               percent)
        {
            nextProgressMark += 10;
        }
    }
}

}

FileSender::FileSender(
    RDTSender &sender)
    : rdtSender(sender)
{
}

void FileSender::abortTransfer()
{
    aborted.store(true);

    if (swSender != nullptr)
    {
        // Abort active Go-Back-N
        // transfer as well.
        swSender->abortTransfer();
    }
}

void FileSender::resetSession()
{
    session =
        TransferSession();
}

void FileSender::setWindowSender(
    SlidingWindowSender *sw)
{
    swSender =
        sw;
}

bool FileSender::sendFile(
    const std::string &filePath,
    const std::string &receiverIp,
    unsigned short receiverPort)
{
    resetSession();

    session.remoteIp =
        receiverIp;

    session.remotePort =
        receiverPort;

    session.fileName =
        filePath;

    aborted.store(false);

    std::ifstream file(
        filePath,
        std::ios::binary);

    if (!file.is_open())
    {
        log_error(
            "Cannot open file: "
            + filePath);

        return false;
    }

    // ==========================================
    // Determine file size.
    // ==========================================

    file.seekg(
        0,
        std::ios::end);

    const std::streampos endPosition =
        file.tellg();

    if (endPosition < 0)
    {
        log_error(
            "Cannot determine file size: "
            + filePath);

        file.close();

        return false;
    }

    const std::uint64_t fileSize =
        static_cast<
            std::uint64_t>(
            endPosition);

    file.seekg(
        0,
        std::ios::beg);

    session.fileSize =
        fileSize;

    // ==========================================
    // Build metadata.
    // ==========================================

    FileMetadata meta{};

    const std::string filename =
        std::filesystem::path(
            filePath)
            .filename()
            .string();

    std::strncpy(
        meta.fileName,
        filename.c_str(),
        MAX_FILENAME_LENGTH - 1);

    meta.fileName[
        MAX_FILENAME_LENGTH - 1] =
        '\0';

    meta.fileSize =
        fileSize;

    const std::string modeStr =
        (swSender != nullptr)
            ? ("Go-Back-N (W="
               + std::to_string(
                   SlidingWindowSender::
                       WINDOW_SIZE)
               + ")")
            : "Stop-and-Wait";

    log_info(
        "================================");

    log_info(
        "Transfer Start ("
        + modeStr
        + ")");

    log_info(
        "File   : "
        + filePath);

    log_info(
        "Size   : "
        + std::to_string(
            fileSize)
        + " bytes");

    log_info(
        "Remote : "
        + receiverIp
        + ":"
        + std::to_string(
            receiverPort));

    log_info(
        "================================");

    const auto startTime =
        Clock::now();

    const auto elapsedSec =
        [&]() -> double
    {
        return
            std::chrono::
                duration<double>(
                    Clock::now()
                    - startTime)
                .count();
    };

    // ==========================================
    // 1. SEND META
    // ==========================================

    const auto metaPacket =
        PacketBuilder::
            buildMetaPacket(
                session.nextSeq,
                meta);

    bool metaSuccess =
        false;

    if (swSender != nullptr)
    {
        swSender->beginSession(
            receiverIp,
            receiverPort);

        metaSuccess =
            swSender->send(
                metaPacket)
            &&
            swSender->flush();
    }
    else
    {
        metaSuccess =
            rdtSender.send(
                metaPacket,
                receiverIp,
                receiverPort);
    }

    if (!metaSuccess)
    {
        if (aborted.load())
        {
            log_info(
                "FileSender: transfer "
                "aborted during META send.");
        }
        else
        {
            log_error(
                "Failed to send metadata "
                "at seq="
                + std::to_string(
                    session.nextSeq));
        }

        file.close();

        return false;
    }

    log_info(
        "Metadata sent. Name="
        + std::string(
            meta.fileName)
        + " Size="
        + std::to_string(
            meta.fileSize));

    session.nextSeq++;

    // ==========================================
    // 2. SEND DATA
    // ==========================================

    char buffer[
        MAX_PAYLOAD_SIZE];

    int nextProgressMark =
        10;

    // Empty file should still
    // display a completed transfer.
    if (session.fileSize == 0)
    {
        printSendProgress(
            0,
            0,
            elapsedSec(),
            nextProgressMark);
    }

    while (true)
    {
        if (aborted.load())
        {
            log_info(
                "FileSender: aborted "
                "by ABOR command.");

            if (file.is_open())
            {
                file.close();
            }

            return false;
        }

        file.read(
            buffer,
            MAX_PAYLOAD_SIZE);

        if (file.bad())
        {
            log_error(
                "Read file failed.");

            file.close();

            return false;
        }

        const std::streamsize
            bytesRead =
                file.gcount();

        if (bytesRead <= 0)
        {
            break;
        }

        const auto packet =
            PacketBuilder::
                buildDataPacket(
                    session.nextSeq,
                    buffer,
                    static_cast<
                        std::uint16_t>(
                        bytesRead));

        bool dataSuccess =
            false;

        if (swSender != nullptr)
        {
            dataSuccess =
                swSender->send(
                    packet);
        }
        else
        {
            dataSuccess =
                rdtSender.send(
                    packet,
                    receiverIp,
                    receiverPort);
        }

        if (!dataSuccess)
        {
            if (aborted.load())
            {
                log_info(
                    "FileSender: transfer "
                    "aborted during UDP send.");
            }
            else
            {
                log_error(
                    "FileSender: UDP send "
                    "failed at seq="
                    + std::to_string(
                        session.nextSeq));
            }

            if (file.is_open())
            {
                file.close();
            }

            return false;
        }

        session.nextSeq++;

        session.bytesTransferred +=
            static_cast<
                std::uint64_t>(
                bytesRead);

        session.packetsTransferred++;

        printSendProgress(
            session.bytesTransferred,
            session.fileSize,
            elapsedSec(),
            nextProgressMark);
    }

    file.close();

    // Progress bar uses '\r', so
    // move following logs to a new line.
    std::cout
        << std::endl;

    // ==========================================
    // 3. SEND FIN
    // ==========================================

    const auto finPacket =
        PacketBuilder::
            buildFinPacket(
                session.nextSeq);

    bool finSuccess =
        false;

    if (swSender != nullptr)
    {
        finSuccess =
            swSender->send(
                finPacket)
            &&
            swSender->flush();
    }
    else
    {
        finSuccess =
            rdtSender.send(
                finPacket,
                receiverIp,
                receiverPort);
    }

    if (!finSuccess)
    {
        if (aborted.load())
        {
            log_info(
                "FileSender: transfer "
                "aborted during FIN send.");
        }
        else
        {
            log_error(
                "Failed to send/flush FIN.");
        }

        return false;
    }

    log_info(
        "FIN sent and ACKed.");

    session.finished =
        true;

    printSummary(
        elapsedSec());

    return true;
}

void FileSender::printSummary(
    double elapsedSec) const
{
    const double speedMBs =
        (elapsedSec > 0.001)
            ? (session.bytesTransferred
               / 1048576.0
               / elapsedSec)
            : 0.0;

    std::ostringstream
        timeStr;

    std::ostringstream
        speedStr;

    timeStr
        << std::fixed
        << std::setprecision(2)
        << elapsedSec
        << " s";

    speedStr
        << std::fixed
        << std::setprecision(2)
        << speedMBs
        << " MB/s";

    const std::string modeStr =
        (swSender != nullptr)
            ? ("Go-Back-N (W="
               + std::to_string(
                   SlidingWindowSender::
                       WINDOW_SIZE)
               + ")")
            : "Stop-and-Wait";

    log_info(
        "================================");

    log_info(
        "Transfer Summary");

    log_info(
        "File     : "
        + session.fileName);

    log_info(
        "Packets  : "
        + std::to_string(
            session.packetsTransferred));

    log_info(
        "Bytes    : "
        + std::to_string(
            session.bytesTransferred));

    log_info(
        "Size     : "
        + std::to_string(
            session.fileSize));

    log_info(
        "Time     : "
        + timeStr.str());

    log_info(
        "Speed    : "
        + speedStr.str());

    log_info(
        "Mode     : "
        + modeStr);

    log_info(
        "Finished : "
        + std::string(
            session.finished
                ? "Yes"
                : "No"));

    log_info(
        "================================");
}

const TransferSession &
FileSender::getSession() const
{
    return session;
}