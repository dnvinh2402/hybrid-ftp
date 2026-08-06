#include <fstream>
#include "file_receiver.h"
FileReceiver::FileReceiver(RDTReceiver &receiver)
    : rdtReceiver(receiver)
{
}
void FileReceiver::resetSession()
{
    session = TransferSession();
}

bool FileReceiver::receiveFile(const std::string &outputFile)
{
    resetSession();

    session.fileName = outputFile;
    log_info("Receiving file : " + outputFile);
    std::ofstream file(outputFile, std::ios::binary);

    if (!file.is_open())
    {
        log_error("Cannot create output file.");

        return false;
    }

    while (!session.finished)
    {
        RDTPacket packet;

        std::string ip;

        unsigned short port;

        if (!rdtReceiver.receive(packet, ip, port))
            continue;

        session.remoteIp = ip;
        session.remotePort = port;

        if (session.packetsTransferred == 0)
        {
            log_info(
                "Connected sender : " + session.remoteIp + ":" + std::to_string(session.remotePort));
        }

        if (packet.header.flags & RDTFlag::FIN)
        {
            session.finished = true;

            log_info("FIN received.");
            log_info("Transfer finished by sender.");

            break;
        }
        if (isUnexpectedPacket(packet))
        {
            log_info(
                "Duplicate packet " + std::to_string(packet.header.seq_num) + " ignored.");
            continue;
        }

        file.write(
            packet.payload,
            static_cast<std::streamsize>(
                packet.header.payload_len));
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

    file.close();

    log_info("Receive complete.");
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

    log_info("--------------------------------");
}
bool FileReceiver::isUnexpectedPacket(const RDTPacket &packet)
{
    return packet.header.seq_num != session.expectedSeq;
}

const TransferSession& FileReceiver::getSession() const
{
    return session;
}