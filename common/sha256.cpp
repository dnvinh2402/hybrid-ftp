#include "sha256.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
    // SHA-256 works with 32-bit words.
    using Word = std::uint32_t;

    Word rotateRight(Word value, unsigned int amount)
    {
        return (value >> amount) | (value << (32 - amount));
    }

    Word choose(Word x, Word y, Word z)
    {
        return (x & y) ^ (~x & z);
    }

    Word majority(Word x, Word y, Word z)
    {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    Word bigSigma0(Word x)
    {
        return rotateRight(x, 2) ^ rotateRight(x, 13) ^ rotateRight(x, 22);
    }

    Word bigSigma1(Word x)
    {
        return rotateRight(x, 6) ^ rotateRight(x, 11) ^ rotateRight(x, 25);
    }

    Word smallSigma0(Word x)
    {
        return rotateRight(x, 7) ^ rotateRight(x, 18) ^ (x >> 3);
    }

    Word smallSigma1(Word x)
    {
        return rotateRight(x, 17) ^ rotateRight(x, 19) ^ (x >> 10);
    }

    const std::array<Word, 64> ROUND_CONSTANTS = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    std::vector<std::uint8_t> addPadding(const std::vector<std::uint8_t>& originalData)
    {
        std::vector<std::uint8_t> paddedData = originalData;

        // SHA-256 first appends one '1' bit. In byte form this starts with 0x80.
        paddedData.push_back(0x80);

        // Leave 8 bytes at the end for the original message length in bits.
        while ((paddedData.size() % 64) != 56)
        {
            paddedData.push_back(0x00);
        }

        const std::uint64_t originalBitLength = static_cast<std::uint64_t>(originalData.size()) * 8;

        // Store the 64-bit length in big-endian order.
        for (int byteIndex = 7; byteIndex >= 0; --byteIndex)
        {
            const std::uint8_t currentByte = static_cast<std::uint8_t>((originalBitLength >> (byteIndex * 8)) & 0xFF);

            paddedData.push_back(currentByte);
        }

        return paddedData;
    }

    std::string wordsToHex(const std::array<Word, 8>& hashWords)
    {
        std::ostringstream output;
        output << std::hex << std::setfill('0');

        for (Word word : hashWords)
        {
            output << std::setw(8) << word;
        }

        return output.str();
    }
}

std::string SHA256::hashBytes(const std::vector<std::uint8_t>& data)
{
    const std::vector<std::uint8_t> paddedData = addPadding(data);

    // Initial SHA-256 hash values defined by the standard.
    std::array<Word, 8> hashWords = {
        0x6a09e667,
        0xbb67ae85,
        0x3c6ef372,
        0xa54ff53a,
        0x510e527f,
        0x9b05688c,
        0x1f83d9ab,
        0x5be0cd19
    };

    // Process one 512-bit block = 64 bytes at a time.
    for (std::size_t blockStart = 0; blockStart < paddedData.size(); blockStart += 64)
    {
        std::array<Word, 64> messageSchedule{};

        // The first 16 words come directly from the current 64-byte block.
        for (std::size_t wordIndex = 0; wordIndex < 16; ++wordIndex)
        {
            const std::size_t byteIndex = blockStart + wordIndex * 4;

            messageSchedule[wordIndex] =
                (static_cast<Word>(paddedData[byteIndex]) << 24) |
                (static_cast<Word>(paddedData[byteIndex + 1]) << 16) |
                (static_cast<Word>(paddedData[byteIndex + 2]) << 8) |
                static_cast<Word>(paddedData[byteIndex + 3]);
        }

        // Expand 16 original words into 64 words used by the 64 rounds.
        for (std::size_t wordIndex = 16; wordIndex < 64; ++wordIndex)
        {
            messageSchedule[wordIndex] =
                smallSigma1(messageSchedule[wordIndex - 2]) +
                messageSchedule[wordIndex - 7] +
                smallSigma0(messageSchedule[wordIndex - 15]) +
                messageSchedule[wordIndex - 16];
        }

        Word a = hashWords[0];
        Word b = hashWords[1];
        Word c = hashWords[2];
        Word d = hashWords[3];
        Word e = hashWords[4];
        Word f = hashWords[5];
        Word g = hashWords[6];
        Word h = hashWords[7];

        // Main SHA-256 compression loop: 64 rounds.
        for (std::size_t round = 0; round < 64; ++round)
        {
            const Word temporary1 =
                h +
                bigSigma1(e) +
                choose(e, f, g) +
                ROUND_CONSTANTS[round] +
                messageSchedule[round];

            const Word temporary2 = bigSigma0(a) + majority(a, b, c);

            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }

        // Add this block's result into the running hash state.
        hashWords[0] += a;
        hashWords[1] += b;
        hashWords[2] += c;
        hashWords[3] += d;
        hashWords[4] += e;
        hashWords[5] += f;
        hashWords[6] += g;
        hashWords[7] += h;
    }

    return wordsToHex(hashWords);
}

std::string SHA256::hashFile(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        return "";
    }

    std::vector<std::uint8_t> data;
    char byte = 0;

    while (file.get(byte))
    {
        data.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
    }

    return hashBytes(data);
}