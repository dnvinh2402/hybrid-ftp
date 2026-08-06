#include "file_sender.h"

#include "rdt_sender.h"
#include "packet_builder.h"

#include "../common/logger.h"
#include "../common/rdt_packet.h"

#include <fstream>

FileSender::FileSender(RDTSender &sender)
    : rdtSender(sender)
{
}
void FileSender::resetSession()
{
    session = TransferSession();
}
bool FileSender::sendFile(const std::string &filePath,
                          const std::string &receiverIp,
                          unsigned short receiverPort)
{
    resetSession();

    session.remoteIp = receiverIp;
    session.remotePort = receiverPort;
    session.fileName = filePath;

    std::ifstream file(filePath, std::ios::binary);

    if (!file.is_open())
    {
        log_error("Cannot open file.");
        return false;
    }

    log_info("Sending file : " + filePath);
    log_info("--------------------------------");

    log_info("Transfer Start");

    log_info("File : " + filePath);

    log_info("Remote : " + session.remoteIp + ":" + std::to_string(session.remotePort));

    log_info(
        "Payload size : " + std::to_string(MAX_PAYLOAD_SIZE));

    log_info("--------------------------------");

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

        auto packet =
            PacketBuilder::buildDataPacket(
                session.nextSeq,
                buffer,
                static_cast<uint16_t>(bytesRead));

        if (!rdtSender.send(
                packet,
                session.remoteIp,
                session.remotePort))
        {
            log_error("Failed to send packet.");

            file.close();

            return false;
        }

        log_info(
            "Packet " + std::to_string(session.nextSeq) + " sent. Size=" + std::to_string(bytesRead));

        session.nextSeq++;

        session.bytesTransferred += bytesRead;

        session.packetsTransferred++;
    }

    file.close();
    auto finPacket =
        PacketBuilder::buildFinPacket(session.nextSeq);

    if (!rdtSender.send(
            finPacket,
            session.remoteIp,
            session.remotePort))
    {
        log_error("Failed to send FIN.");
        return false;
    }

    log_info("FIN sent.");
    session.finished = true;

    printSummary();

    return true;
}
void FileSender::printSummary() const
{
    log_info("--------------------------------");

    log_info("Transfer Summary");

    log_info("File : " + session.fileName);

    log_info("Packets : " + std::to_string(session.packetsTransferred));

    log_info("Bytes : " + std::to_string(session.bytesTransferred));

    log_info(
        "Finished : " + std::string(session.finished ? "Yes" : "No"));

    log_info("--------------------------------");
}
const TransferSession&
FileSender::getSession() const
{
    return session;
}