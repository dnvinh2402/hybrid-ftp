#include "../network/data_channel.h"
#include <iostream>

int main()
{
    DataChannel channel;

    DataChannelConfig config;
    config.localPort = 5000;
    config.timeout = 1000;

    if(!channel.open(config))
    {
        std::cout<<"Open failed\n";
        return 1;
    }

    std::cout<<"Opened = "<<channel.isOpened()<<std::endl;

    channel.close();

    std::cout<<"Opened = "<<channel.isOpened()<<std::endl;

    return 0;
}