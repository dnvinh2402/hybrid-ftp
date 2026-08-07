#include <fstream>
#include "file_receiver.h"
#include <filesystem>
FileReceiver::FileReceiver(RDTReceiver &receiver)
    : rdtReceiver(receiver)
{
}
void FileReceiver::resetSession()
{
    session = TransferSession();
}

bool FileReceiver::receiveFile(const std::string &saveDirectory)
{
    resetSession();
    FileMetadata metadata{};

    bool metadataReceived = false;

    std::ofstream file;

    while (!session.finished)
    {
        RDTPacket packet;

        std::string ip;

        unsigned short port;

        if (!rdtReceiver.receive(packet, ip, port))
            continue;

        bool isMeta =
            PacketParser::parseMetadata(
                packet,
                metadata);

        if (packet.header.version != RDT_VERSION)
        {
            log_error("Unsupported protocol version.");

            return false;
        }
        if (packet.header.magic != RDT_MAGIC)
        {
            log_error("Invalid protocol.");

            return false;
        }

        if (!metadataReceived && !isMeta)
        {
            log_error("First packet must be META.");

            return false;
        }

        session.remoteIp = ip;
        session.remotePort = port;

        if (isMeta)
        {
            if (metadataReceived)
            {
                log_error("Duplicate META packet.");
                continue;
            }

            session.fileName = metadata.fileName;
            session.fileSize = metadata.fileSize;

            log_info(
                "Connected sender : " + session.remoteIp + ":" + std::to_string(session.remotePort));
            log_info(
                "File name : " + session.fileName);

            log_info(
                "File size : " + std::to_string(session.fileSize));

            std::filesystem::create_directories(saveDirectory);

            std::string outputPath =
                saveDirectory + "/" +
                std::string(metadata.fileName);

            file.open(outputPath,
                      std::ios::binary);
            if (!file.is_open())
            {
                log_error("Cannot create output file.");
                return false;
            }

            log_info("Output file created.");

            metadataReceived = true;

            session.expectedSeq = packet.header.seq_num + 1;

            log_info("--------------------------------");

            log_info("Receive Start");

            log_info("File : " + session.fileName);

            log_info("Size : " +
                     std::to_string(session.fileSize));

            log_info("--------------------------------");
            log_info(
                "Metadata received.");

            continue;
        }

        if (PacketParser::isFin(packet))
        {
            if (packet.header.seq_num != session.expectedSeq)
            {
                log_error(
                    "Unexpected FIN.");

                continue;
            }

            session.finished = true;

            log_info("FIN received.");

            log_info("Transfer finished by sender.");

            break;
        }
        if (!metadataReceived)
        {
            log_error("Metadata missing.");

            return false;
        }

        if (packet.header.seq_num < session.expectedSeq)
        {
            log_info(
                "Duplicate packet " + std::to_string(packet.header.seq_num));

            // Gửi ACK lại
            rdtReceiver.sendAck(
                packet.header.seq_num,
                session.remoteIp,
                session.remotePort);

            continue;
        }

        if (packet.header.seq_num > session.expectedSeq)
        {
            log_error("Out-of-order packet.");
            continue;
        }

        if (!PacketParser::isData(packet))
        {
            log_error("Unexpected packet.");

            continue;
        }
        file.write(
            packet.payload,
            static_cast<std::streamsize>(
                packet.header.payload_len));

        // file.flush();
        // flush se bi cham

        if (file.fail())
        {
            log_error("Write file failed.");
            file.close();
            return false;
        }
        session.expectedSeq++;

        session.bytesTransferred += packet.header.payload_len;

        session.packetsTransferred++;

        log_info(
            "Packet " + std::to_string(packet.header.seq_num) + " received (" + std::to_string(packet.header.payload_len) + " bytes).");
    }
    if (file.is_open())
    {
        file.close();
    }

    log_info("Receive complete.");
    if (session.bytesTransferred != session.fileSize)
    {
        log_error("File size mismatch.");
        return false;
    }
    else
    {
        log_info("File verified.");
    }
    printSummary();
    return true;
}
void FileReceiver::printSummary() const
{
    log_info("--------------------------------");

    log_info("Receive Summary");

    log_info("File      : " + session.fileName);

    log_info("Sender    : " + session.remoteIp + ":" +
             std::to_string(session.remotePort));

    log_info("Packets   : " +
             std::to_string(session.packetsTransferred));

    log_info("Bytes     : " +
             std::to_string(session.bytesTransferred));

    log_info("Expected Seq : " +
             std::to_string(session.expectedSeq));

    log_info("File Size : " +
             std::to_string(session.fileSize));

    log_info("Finished : " +
             std::string(session.finished ? "Yes" : "No"));

    log_info("--------------------------------");
}
const TransferSession &FileReceiver::getSession() const
{
    return session;
}