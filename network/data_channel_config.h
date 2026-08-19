#pragma once

struct DataChannelConfig
{
    unsigned short localPort = 0;
    int timeout = 5000;
    int maxRetry = 5;
    bool simulateAckLoss = false;
    bool useGBN = true;
};