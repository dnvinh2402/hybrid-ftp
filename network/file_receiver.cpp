#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include "file_receiver.h"
#include "../common/logger.h"

namespace
{
void printReceiveProgress(
    std::uint64_t bytesTransferred,
    std::uint64_t totalBytes,
    int &nextProgressMark)
{
    const int percent =
        (totalBytes == 0)
            ? 100
            : static_cast<int>(
                  (bytesTransferred * 100ULL)
                  / totalBytes);

    if (percent >= nextProgressMark ||
        percent == 100)
    {
        std::cout
            << "[TRANSFER][RECV] Progress: "
            << std::setw(3)
            << percent
            << "% ("
            << bytesTransferred
            << "/"
            << totalBytes
            << " bytes)"
            << std::endl;

        while (nextProgressMark <= percent)
        {
            nextProgressMark += 10;
        }
    }
}
}

FileReceiver::FileReceiver(
    RDTReceiver &receiver)
    : rdtReceiver(receiver)
{
}

void FileReceiver::resetSession()
{
    session = TransferSession();
    aborted.store(false);
}

void FileReceiver::abortTransfer()
{
    aborted.store(true);
}

bool FileReceiver::receiveFile(
    const std::string &outputDir)
{
    resetSession();

    // Reset RDT receiver state for
    // every independent transfer.
    rdtReceiver.resetSession();

    FileMetadata metadata{};

    bool metadataReceived = false;

    std::ofstream file;

    int consecutiveTimeouts = 0;

    int nextProgressMark = 10;

    while (!session.finished)
    {
        // ABOR can be requested
        // from another thread.
        if (aborted.load())
        {
            log_info(
                "FileReceiver: aborted "
                "by ABOR command.");

            if (file.is_open())
            {
                file.close();
            }

            if (metadataReceived)
            {
                std::error_code errorCode;

                std::filesystem::remove(
                    std::filesystem::path(
                        outputDir)
                        / session.fileName,
                    errorCode);
            }

            return false;
        }

        RDTPacket packet{};

        std::string ip;

        unsigned short port = 0;

        if (!rdtReceiver.receive(
                packet,
                ip,
                port))
        {
            if (aborted.load())
            {
                log_info("FileReceiver: aborted while waiting for UDP data.");

                if (file.is_open())
                {
                    file.close();
                }

                if (metadataReceived)
                {
                    std::error_code errorCode;
                    std::filesystem::remove(
                        std::filesystem::path(outputDir) / session.fileName,
                        errorCode);
                }

                return false;
            }

            consecutiveTimeouts++;

            log_error(
                "Receive timeout ("
                + std::to_string(
                    consecutiveTimeouts)
                + "/"
                + std::to_string(
                    MAX_CONSECUTIVE_TIMEOUTS)
                + ").");

            if (consecutiveTimeouts >=
                MAX_CONSECUTIVE_TIMEOUTS)
            {
                log_error(
                    "Sender unresponsive. "
                    "Aborting transfer.");

                if (file.is_open())
                {
                    file.close();
                }

                if (metadataReceived)
                {
                    std::error_code errorCode;

                    std::filesystem::remove(
                        std::filesystem::path(
                            outputDir)
                            / session.fileName,
                        errorCode);
                }

                return false;
            }

            continue;
        }

        consecutiveTimeouts = 0;

        // Validate packet header.
        if (packet.header.version !=
            RDT_VERSION)
        {
            log_error(
                "Unsupported protocol version.");

            if (file.is_open())
            {
                file.close();
            }

            return false;
        }

        if (packet.header.magic !=
            RDT_MAGIC)
        {
            log_error(
                "Invalid protocol magic.");

            if (file.is_open())
            {
                file.close();
            }

            return false;
        }

        // Data-plane ABOR packet.
        if (packet.header.flags &
            RDTFlag::ABOR)
        {
            log_info(
                "FileReceiver: ABOR flag "
                "received from peer data channel.");

            if (file.is_open())
            {
                file.close();
            }

            if (metadataReceived)
            {
                std::error_code errorCode;

                std::filesystem::remove(
                    std::filesystem::path(
                        outputDir)
                        / session.fileName,
                    errorCode);
            }

            return false;
        }

        const bool isMeta =
            PacketParser::parseMetadata(
                packet,
                metadata);

        if (!metadataReceived &&
            !isMeta)
        {
            log_error(
                "First packet must be META.");

            if (file.is_open())
            {
                file.close();
            }

            return false;
        }

        session.remoteIp =
            ip;

        session.remotePort =
            port;

        // ==========================================
        // 1. META PACKET
        // ==========================================

        if (isMeta)
        {
            if (metadataReceived)
            {
                log_info(
                    "Duplicate META packet "
                    "(retransmit), re-ACKed.");

                rdtReceiver.sendAck(
                    packet.header.seq_num,
                    ip,
                    port);

                continue;
            }

            session.fileName =
                metadata.fileName;

            session.fileSize =
                metadata.fileSize;

            std::filesystem::
                create_directories(
                    outputDir);

            const std::filesystem::path
                outputPath =
                    std::filesystem::path(
                        outputDir)
                    / session.fileName;

            if (session.fileSize == 0)
            {
                printReceiveProgress(
                    0,
                    0,
                    nextProgressMark);
            }

            log_info(
                "Connected sender : "
                + session.remoteIp
                + ":"
                + std::to_string(
                    session.remotePort));

            log_info(
                "File name        : "
                + session.fileName);

            log_info(
                "File size        : "
                + std::to_string(
                    session.fileSize));

            file.open(
                outputPath,
                std::ios::binary);

            if (!file.is_open())
            {
                log_error(
                    "Cannot create output file: "
                    + outputPath.string());

                return false;
            }

            metadataReceived =
                true;

            session.expectedSeq =
                packet.header.seq_num + 1;

            // Explicit META ACK retained
            // for Go-Back-N behavior.
            rdtReceiver.sendAck(
                packet.header.seq_num,
                ip,
                port);

            log_info(
                "================================");

            log_info(
                "Receive Start");

            log_info(
                "File : "
                + session.fileName);

            log_info(
                "Size : "
                + std::to_string(
                    session.fileSize));

            log_info(
                "================================");

            log_info(
                "Metadata received.");

            continue;
        }

        // ==========================================
        // 2. FIN PACKET
        // ==========================================

        if (PacketParser::isFin(
                packet))
        {
            if (packet.header.seq_num !=
                session.expectedSeq)
            {
                if (session.expectedSeq >
                    0)
                {
                    rdtReceiver.sendAck(
                        session.expectedSeq - 1,
                        ip,
                        port);
                }

                continue;
            }

            rdtReceiver.sendAck(
                packet.header.seq_num,
                ip,
                port);

            session.finished =
                true;

            log_info(
                "FIN received. "
                "Transfer finished.");

            break;
        }

        if (!metadataReceived)
        {
            log_error(
                "Metadata missing.");

            if (file.is_open())
            {
                file.close();
            }

            return false;
        }

        // ==========================================
        // 3. DUPLICATE PACKET
        // ==========================================

        if (packet.header.seq_num <
            session.expectedSeq)
        {
            rdtReceiver.sendAck(
                packet.header.seq_num,
                ip,
                port);

            continue;
        }

        // ==========================================
        // 4. OUT-OF-ORDER PACKET
        //    Go-Back-N receiver does not buffer it.
        // ==========================================

        if (packet.header.seq_num >
            session.expectedSeq)
        {
            if (session.expectedSeq >
                0)
            {
                rdtReceiver.sendAck(
                    session.expectedSeq - 1,
                    ip,
                    port);
            }

            continue;
        }

        // ==========================================
        // 5. DATA PACKET
        // ==========================================

        if (!PacketParser::isData(
                packet))
        {
            log_error(
                "Unexpected packet type at seq="
                + std::to_string(
                    packet.header.seq_num));

            continue;
        }

        file.write(
            packet.payload,
            static_cast<
                std::streamsize>(
                packet.header.payload_len));

        if (file.fail())
        {
            log_error(
                "Write file failed.");

            file.close();

            return false;
        }

        // ACK only after payload
        // has been written successfully.
        rdtReceiver.sendAck(
            packet.header.seq_num,
            ip,
            port);

        session.expectedSeq++;

        session.bytesTransferred +=
            packet.header.payload_len;

        session.packetsTransferred++;

        printReceiveProgress(
            session.bytesTransferred,
            session.fileSize,
            nextProgressMark);
    }

    if (file.is_open())
    {
        file.close();
    }

    log_info(
        "Receive complete.");

    if (session.bytesTransferred !=
        session.fileSize)
    {
        log_error(
            "File size mismatch: got="
            + std::to_string(
                session.bytesTransferred)
            + " expected="
            + std::to_string(
                session.fileSize));

        return false;
    }

    log_info(
        "File verified OK.");

    printSummary();

    return true;
}

void FileReceiver::printSummary() const
{
    log_info(
        "================================");

    log_info(
        "Receive Summary");

    log_info(
        "File         : "
        + session.fileName);

    log_info(
        "Sender       : "
        + session.remoteIp
        + ":"
        + std::to_string(
            session.remotePort));

    log_info(
        "Packets      : "
        + std::to_string(
            session.packetsTransferred));

    log_info(
        "Bytes        : "
        + std::to_string(
            session.bytesTransferred));

    log_info(
        "Expected Seq : "
        + std::to_string(
            session.expectedSeq));

    log_info(
        "File Size    : "
        + std::to_string(
            session.fileSize));

    log_info(
        "Finished     : "
        + std::string(
            session.finished
                ? "Yes"
                : "No"));

    log_info(
        "================================");
}

const TransferSession &
FileReceiver::getSession() const
{
    return session;
}