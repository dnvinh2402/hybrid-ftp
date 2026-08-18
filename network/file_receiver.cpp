#include <fstream>
#include <filesystem>
#include "file_receiver.h"
#include "../common/logger.h"

FileReceiver::FileReceiver(RDTReceiver &receiver)
    : rdtReceiver(receiver)
{
}

void FileReceiver::resetSession()
{
    session = TransferSession();
    // aborted.store(false);   // reset trước mỗi phiên nhận mới
}

bool FileReceiver::receiveFile()
{
    resetSession();
    // Reset trạng thái RDTReceiver (bộ đếm, cờ firstPacketOfSession) để
    // mỗi phiên nhận hoạt động độc lập.
    rdtReceiver.resetSession();

    FileMetadata metadata{};
    bool         metadataReceived    = false;
    std::ofstream file;
    int           consecutiveTimeouts = 0;

    while (!session.finished)
    {
        // Kiểm tra abort flag — được set bởi ABOR handler trên thread khác
        // if (aborted.load())
        // {
        //     log_info("FileReceiver: aborted by ABOR command.");
        //     if (file.is_open()) file.close();
        //     return false;
        // }
        RDTPacket     packet;
        std::string   ip;
        unsigned short port;

        if (!rdtReceiver.receive(packet, ip, port))
        {
            consecutiveTimeouts++;
            log_error("Receive timeout (" + std::to_string(consecutiveTimeouts) +
                      "/" + std::to_string(MAX_CONSECUTIVE_TIMEOUTS) + ").");

            if (consecutiveTimeouts >= MAX_CONSECUTIVE_TIMEOUTS)
            {
                log_error("Sender unresponsive. Aborting transfer.");
                if (file.is_open()) file.close();
                return false;
            }
            continue;
        }
        consecutiveTimeouts = 0;

        // ── Kiểm tra version / magic trước ──────────────────────────────
        if (packet.header.version != RDT_VERSION)
        {
            log_error("Unsupported protocol version.");
            if (file.is_open()) file.close();
            return false;
        }
        if (packet.header.magic != RDT_MAGIC)
        {
            log_error("Invalid protocol magic.");
            if (file.is_open()) file.close();
            return false;
        }

        // ── Parse metadata ───────────────────────────────────────────────
        bool isMeta = PacketParser::parseMetadata(packet, metadata);

        if (!metadataReceived && !isMeta)
        {
            log_error("First packet must be META.");
            if (file.is_open()) file.close();
            return false;
        }

        session.remoteIp   = ip;
        session.remotePort = port;

        // ── META packet ──────────────────────────────────────────────────
        if (isMeta)
        {
            if (metadataReceived)
            {
                log_info("Duplicate META packet (retransmit), re-ACKed.");
                // META đã được ACK bởi RDTReceiver::receive() → chỉ skip
                continue;
            }

            session.fileName = metadata.fileName;
            session.fileSize = metadata.fileSize;

            log_info("Connected sender : " + ip + ":" + std::to_string(port));
            log_info("File name        : " + session.fileName);
            log_info("File size        : " + std::to_string(session.fileSize));

            std::filesystem::create_directories("server_files");
            std::string outputPath = "server_files/" + std::string(metadata.fileName);

            file.open(outputPath, std::ios::binary);
            if (!file.is_open())
            {
                log_error("Cannot create output file: " + outputPath);
                return false;
            }

            metadataReceived    = true;
            session.expectedSeq = packet.header.seq_num + 1;

            log_info("--------------------------------");
            log_info("Receive Start");
            log_info("File : " + session.fileName);
            log_info("Size : " + std::to_string(session.fileSize));
            log_info("--------------------------------");
            continue;
        }

        // ── FIN packet ───────────────────────────────────────────────────
        if (PacketParser::isFin(packet))
        {
            if (packet.header.seq_num != session.expectedSeq)
            {
                // GBN: FIN out-of-order → discard + cumulative ACK
                log_info("[GBN] FIN out-of-order seq=" +
                         std::to_string(packet.header.seq_num) +
                         " expected=" + std::to_string(session.expectedSeq));
                if (session.expectedSeq > 0)
                    rdtReceiver.sendAck(session.expectedSeq - 1, ip, port);
                continue;
            }
            session.finished = true;
            log_info("FIN received. Transfer finished.");
            break;
        }

        if (!metadataReceived)
        {
            log_error("Metadata missing.");
            if (file.is_open()) file.close();
            return false;
        }

        // ── Duplicate packet (seq < expected) ────────────────────────────
        // Xảy ra khi GBN sender retransmit từ đầu cửa sổ: receiver đã ghi
        // gói này rồi, chỉ cần gửi lại ACK để unblock sender.
        if (packet.header.seq_num < session.expectedSeq)
        {
            log_info("[GBN] Duplicate seq=" + std::to_string(packet.header.seq_num) +
                     " (expected=" + std::to_string(session.expectedSeq) +
                     "). Resending ACK.");
            rdtReceiver.sendAck(packet.header.seq_num, ip, port);
            continue;
        }

        // ── Out-of-order packet (seq > expected) — GBN: discard ──────────
        // GBN receiver KHÔNG buffer out-of-order. Gửi lại cumulative ACK
        // của packet cuối đã nhận đúng thứ tự để trigger GBN retransmit.
        if (packet.header.seq_num > session.expectedSeq)
        {
            log_info("[GBN] Out-of-order seq=" + std::to_string(packet.header.seq_num) +
                     " expected=" + std::to_string(session.expectedSeq) +
                     ". Discarding, resending cumulative ACK.");
            if (session.expectedSeq > 0)
                rdtReceiver.sendAck(session.expectedSeq - 1, ip, port);
            // expectedSeq = 0 → chưa nhận gói nào sau META → không gửi ACK
            // (sender sẽ timeout và retransmit từ seq 1)
            continue;
        }

        // ── Đúng thứ tự: DATA packet ─────────────────────────────────────
        if (!PacketParser::isData(packet))
        {
            log_error("Unexpected packet type at seq=" +
                      std::to_string(packet.header.seq_num));
            continue;
        }

        file.write(packet.payload,
                   static_cast<std::streamsize>(packet.header.payload_len));

        if (file.fail())
        {
            log_error("Write file failed.");
            file.close();
            return false;
        }

        session.expectedSeq++;
        session.bytesTransferred  += packet.header.payload_len;
        session.packetsTransferred++;

        //tatlog
        // log_info("Packet seq=" + std::to_string(packet.header.seq_num) +
        //          " received (" + std::to_string(packet.header.payload_len) +
        //          " bytes). Total=" + std::to_string(session.bytesTransferred));
    }

    if (file.is_open())
        file.close();

    log_info("Receive complete.");

    if (session.bytesTransferred != session.fileSize)
    {
        log_error("File size mismatch: got=" + std::to_string(session.bytesTransferred) +
                  " expected=" + std::to_string(session.fileSize));
        return false;
    }

    log_info("File verified OK.");
    printSummary();
    return true;
}

void FileReceiver::printSummary() const
{
    log_info("--------------------------------");
    log_info("Receive Summary");
    log_info("File         : " + session.fileName);
    log_info("Sender       : " + session.remoteIp + ":" +
             std::to_string(session.remotePort));
    log_info("Packets      : " + std::to_string(session.packetsTransferred));
    log_info("Bytes        : " + std::to_string(session.bytesTransferred));
    log_info("Expected Seq : " + std::to_string(session.expectedSeq));
    log_info("File Size    : " + std::to_string(session.fileSize));
    log_info("Finished     : " + std::string(session.finished ? "Yes" : "No"));
    log_info("--------------------------------");
}

const TransferSession &FileReceiver::getSession() const
{
    return session;
}