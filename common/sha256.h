#ifndef SHA256_H
#define SHA256_H

#include <cstdint>
#include <string>
#include <vector>

namespace SHA256
{
    // Hash raw bytes and return a 64-character hexadecimal SHA-256 string.
    std::string hashBytes(const std::vector<std::uint8_t>& data);

    // Read a file in binary mode and hash all bytes in the file.
    // Returns an empty string if the file cannot be opened.
    std::string hashFile(const std::string& filePath);
}

#endif