#include "../network/udp_socket.h"
#include "../network/rdt_sender.h"
#include "../network/rdt_receiver.h"
#include "../network/packet_builder.h"

#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib,"ws2_32.lib")
#endif

int main(int argc,char* argv[])
{
#ifdef _WIN32
WSADATA wsa;
WSAStartup(MAKEWORD(2,2),&wsa);
#endif

if(argc!=2)
{
    std::cout<<"Usage:\n";
    std::cout<<"server\n";
    std::cout<<"client\n";
    return 0;
}

UDPSocket udp;

udp.create();

std::string mode=argv[1];

if(mode=="server")
{
    udp.bind(5000);

    RDTReceiver receiver(udp);

    for(int i=0;i<5;i++)
    {
        RDTPacket pkt;

        std::string ip;

        unsigned short port;

        receiver.receive(pkt,ip,port);

        std::cout<<"Receive seq = "
                 <<pkt.header.seq_num
                 <<std::endl;
    }
}
else
{
    RDTSender sender(udp);

    for(int i=0;i<5;i++)
    {
        auto pkt=
        PacketBuilder::buildDataPacket(
            i,
            "Hello",
            5);

        sender.send(
            pkt,
            "127.0.0.1",
            5000);
    }
}

#ifdef _WIN32
WSACleanup();
#endif

}