struct DataChannelConfig
{
    unsigned short localPort = 0;

    int timeout = 1000;

    //sua timeout
    int maxRetry = 5;

    bool simulateAckLoss = false;
};