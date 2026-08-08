#ifndef TRANSFER_SESSION_H
#define TRANSFER_SESSION_H

#include <string>
#include <cstdint>

struct TransferSession
{
    // Peer
    std::string remoteIp;
    unsigned short remotePort = 0;

    // Sequence
    uint32_t nextSeq = 0;
    uint32_t expectedSeq = 0;

    // Statistics
    uint64_t bytesTransferred = 0;
    uint32_t packetsTransferred = 0;

    // File
    std::string fileName;
    uint64_t fileSize = 0;
    
    // State
    bool finished = false;
};
#endif