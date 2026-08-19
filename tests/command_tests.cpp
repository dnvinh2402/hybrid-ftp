#include <array>
#include <cassert>
#include <cctype>
#include <iostream>
#include <string>
#include <utility>

#include "../common/protocol.h"

int main()
{
    const std::array<
        std::pair<
            const char*,
            FtpCommand>,
        28> commands =
    {{
        {
            "USER",
            FtpCommand::USER
        },
        {
            "PASS",
            FtpCommand::PASS
        },
        {
            "QUIT",
            FtpCommand::QUIT
        },
        {
            "NOOP",
            FtpCommand::NOOP
        },
        {
            "PWD",
            FtpCommand::PWD
        },
        {
            "CWD",
            FtpCommand::CWD
        },
        {
            "CDUP",
            FtpCommand::CDUP
        },
        {
            "MKD",
            FtpCommand::MKD
        },
        {
            "RMD",
            FtpCommand::RMD
        },
        {
            "LIST",
            FtpCommand::LIST
        },
        {
            "NLST",
            FtpCommand::NLST
        },
        {
            "STAT",
            FtpCommand::STAT
        },
        {
            "SIZE",
            FtpCommand::SIZE
        },
        {
            "MDTM",
            FtpCommand::MDTM
        },
        {
            "TYPE",
            FtpCommand::TYPE
        },
        {
            "MODE",
            FtpCommand::MODE
        },
        {
            "PORT",
            FtpCommand::PORT
        },
        {
            "PASV",
            FtpCommand::PASV
        },
        {
            "RETR",
            FtpCommand::RETR
        },
        {
            "STOR",
            FtpCommand::STOR
        },
        {
            "STOU",
            FtpCommand::STOU
        },
        {
            "APPE",
            FtpCommand::APPE
        },
        {
            "DELE",
            FtpCommand::DELE
        },
        {
            "RNFR",
            FtpCommand::RNFR
        },
        {
            "RNTO",
            FtpCommand::RNTO
        },
        {
            "HASH",
            FtpCommand::HASH
        },
        {
            "ABOR",
            FtpCommand::ABOR
        },
        {
            "HELP",
            FtpCommand::HELP
        }
    }};

    static_assert(
        commands.size() == 28,
        "The project must expose "
        "exactly 28 required FTP commands.");

    for (const auto& entry : commands)
    {
        const std::string verb =
            entry.first;

        const FtpCommand expected =
            entry.second;

        // Test direct command lookup.
        assert(
            parseCommandVerb(verb)
            == expected);

        // Test lower-case command.
        std::string lower = verb;

        for (char& character : lower)
        {
            character =
                static_cast<char>(
                    std::tolower(
                        static_cast<
                            unsigned char>(
                            character)));
        }

        const ParsedCommand parsed =
            parseLine(
                lower
                + " sample_arg\r\n");

        assert(
            parsed.cmd
            == expected);

        assert(
            parsed.verbRaw
            == verb);

        assert(
            parsed.arg
            == "sample_arg");
    }

    // Unknown command.
    assert(
        parseLine(
            "NOT_A_COMMAND\r\n")
            .cmd
        == FtpCommand::UNKNOWN);

    // STOU is valid without
    // a remote filename.
    const ParsedCommand stou =
        parseLine(
            "STOU\r\n");

    assert(
        stou.cmd
        == FtpCommand::STOU);

    assert(
        stou.arg.empty());

    std::cout
        << "command_tests passed: "
           "all 28 FTP commands recognized\n";

    return 0;
}