#include "../network/file_sender.h"
#include "../network/file_receiver.h"
#include "../network/rdt_sender.h"
#include "../network/rdt_receiver.h"
#include "../network/udp_socket.h"

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib,"ws2_32.lib")
#endif

#include <iostream>

int main(int argc,char* argv[])
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2),&wsa);
#endif

    if(argc!=2)
    {
        std::cout
            << "Usage:\n"
            << "test_file_transfer server\n"
            << "test_file_transfer client\n";
        return 0;
    }

    UDPSocket udp;

    udp.create();

    std::string mode=argv[1];

    if(mode=="server")
    {
        udp.bind(5000);

        RDTReceiver receiver(udp);

        FileReceiver fr(receiver);

        fr.receiveFile("received.png");
        fr.receiveFile("received.txt");
        fr.receiveFile("received.pdf");
    }
    else
    {
        udp.setReceiveTimeout(1000);

        RDTSender sender(udp);

        FileSender fs(sender);

        fs.sendFile(
            "picture.png",
            "127.0.0.1",
            5000);
        fs.sendFile(
            "sample.txt",
            "127.0.0.1",
            5000);
        fs.sendFile(
            "Singleton.pdf",
            "127.0.0.1",
            5000);
    }

#ifdef _WIN32
    WSACleanup();
#endif
}