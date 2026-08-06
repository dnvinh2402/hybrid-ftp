#ifndef FILE_METADATA_H
#define FILE_METADATA_H

#include <cstdint>

constexpr size_t MAX_FILENAME_LENGTH = 256;

struct FileMetadata
{
    char fileName[MAX_FILENAME_LENGTH];

    uint64_t fileSize;
};

#endif