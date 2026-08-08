#include "../network/packet_builder.h"
#include "../network/packet_parser.h"

#include <iostream>

int main()
{
    FileMetadata meta{};

    strcpy(meta.fileName, "sample.pdf");

    meta.fileSize = 123456;

    auto packet =
        PacketBuilder::buildMetaPacket(
            0,
            meta);

    FileMetadata parsed;

    bool ok =
        PacketParser::parseMetadata(
            packet,
            parsed);

    if(ok)
    {
        std::cout
            << "File : "
            << parsed.fileName
            << std::endl;

        std::cout
            << "Size : "
            << parsed.fileSize
            << std::endl;
    }
    else
    {
        std::cout
            << "Parse failed"
            << std::endl;
    }

    return 0;
}