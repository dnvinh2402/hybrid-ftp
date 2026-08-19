#pragma once
struct DataChannelConfig
{
    unsigned short localPort = 0;

    int timeout = 5000;

    //sua timeout
    int maxRetry = 5;

    bool simulateAckLoss = false;
    bool useGBN = true;   // ← THÊM: true = Go-Back-N, false = Stop-and-Wait
    int  windowSize = 8;      
};