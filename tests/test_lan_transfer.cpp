#include "../network/data_channel.h"
#include <iostream>

int main(int argc,char* argv[])
{
    if(argc < 2)
    {
        std::cout
        << "Usage:\n"
        << "server\n"
        << "client <server-ip>\n";

        return 0;
    }

    DataChannel channel;

    DataChannelConfig config;
    config.timeout = 1000;

    std::string mode = argv[1];

    if(mode=="server")
    {
        config.localPort = 5000;

        if(!channel.open(config))
        {
            std::cout<<"Open failed\n";
            return 0;
        }

        std::cout<<"Waiting file...\n";

        channel.receiveFile("server_files");

        channel.close();
    }
    else if(mode=="client")
    {
        if(argc!=3)
        {
            std::cout<<"Need server ip\n";
            return 0;
        }

        config.localPort = 4000;

        if(!channel.open(config))
        {
            std::cout<<"Open failed\n";
            return 0;
        }

        channel.sendFile(
            "client_files/Singleton.pdf",
            argv[2],
            5000);

        channel.close();
    }
}