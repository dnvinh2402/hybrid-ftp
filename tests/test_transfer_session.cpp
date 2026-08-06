#include "../network/transfer_session.h"
#include <iostream>

int main()
{
    TransferSession session;

    std::cout
        << "Remote IP: "
        << session.remoteIp
        << '\n';

    std::cout
        << "Remote Port: "
        << session.remotePort
        << '\n';

    std::cout
        << "Next Seq: "
        << session.nextSeq
        << '\n';

    std::cout
        << "Expected Seq: "
        << session.expectedSeq
        << '\n';

    std::cout
        << "Packets: "
        << session.packetsTransferred
        << '\n';

    std::cout
        << "Bytes: "
        << session.bytesTransferred
        << '\n';

    std::cout
        << "Finished: "
        << std::boolalpha
        << session.finished
        << '\n';
}