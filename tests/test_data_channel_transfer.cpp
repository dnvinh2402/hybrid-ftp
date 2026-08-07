#include "../network/data_channel.h"
#include <iostream>

int main(int argc,char* argv[])
{
    if(argc!=2)
    {
        std::cout<<"server/client\n";
        return 0;
    }

    DataChannel channel;

    DataChannelConfig config;

    config.timeout = 1000;

    std::string mode=argv[1];

    if(mode=="server")
    {
        config.localPort=5000;

        channel.open(config);

        channel.receiveFile("server_files");

        channel.close();
    }
    else
    {
        config.localPort=4000;

        channel.open(config);

        channel.sendFile(
            "client_files/Singleton.pdf",
            "127.0.0.1",
            5000);

        channel.close();
    }

    return 0;
}