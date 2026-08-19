#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <atomic>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>

// Standard FTP reply codes used by the control channel.
namespace FTPStatus
{
    inline const std::string OK_125 =
        "125 Data connection already open; transfer starting.\r\n";
    inline const std::string OK_150 =
        "150 File status okay; opening data connection.\r\n";
    inline const std::string OK_200 = "200 Command OK.\r\n";
    inline const std::string OK_220 = "220 Service ready for new user.\r\n";
    inline const std::string OK_221 = "221 Goodbye.\r\n";
    inline const std::string OK_225 = "225 No transfer in progress.\r\n";
    inline const std::string OK_226 =
        "226 Closing data connection. Requested file action successful.\r\n";
    inline const std::string OK_230 = "230 User logged in, proceed.\r\n";
    inline const std::string OK_250 =
        "250 Requested file action okay, completed.\r\n";
    inline const std::string NEED_PASS_331 = "331 Username OK, need password.\r\n";
    inline const std::string PENDING_350 =
        "350 Requested file action pending further information.\r\n";
    inline const std::string ERR_421 =
        "421 Service not available, closing control connection.\r\n";
    inline const std::string ERR_425 = "425 Can't open data connection.\r\n";
    inline const std::string ERR_426 = "426 Connection closed; transfer aborted.\r\n";
    inline const std::string ERR_450 =
        "450 Requested file action not taken; file unavailable.\r\n";
    inline const std::string ERR_500 =
        "500 Syntax error, command unrecognized.\r\n";
    inline const std::string ERR_501 =
        "501 Syntax error in parameters or arguments.\r\n";
    inline const std::string ERR_502 = "502 Command not implemented.\r\n";
    inline const std::string ERR_530 = "530 Not logged in.\r\n";
    inline const std::string ERR_550 = "550 File unavailable.\r\n";
}

// Exactly 28 required FTP commands plus UNKNOWN for parser failures.
enum class FtpCommand
{
    USER,
    PASS,
    QUIT,
    NOOP,
    PWD,
    CWD,
    CDUP,
    MKD,
    RMD,
    LIST,
    NLST,
    STAT,
    SIZE,
    MDTM,
    TYPE,
    MODE,
    PORT,
    PASV,
    RETR,
    STOR,
    STOU,
    APPE,
    DELE,
    RNFR,
    RNTO,
    HASH,
    ABOR,
    HELP,
    UNKNOWN
};

inline FtpCommand parseCommandVerb(const std::string &verb)
{
    static const std::unordered_map<std::string, FtpCommand> table = {
        {"USER", FtpCommand::USER}, {"PASS", FtpCommand::PASS},
        {"QUIT", FtpCommand::QUIT}, {"NOOP", FtpCommand::NOOP},
        {"PWD", FtpCommand::PWD},   {"CWD", FtpCommand::CWD},
        {"CDUP", FtpCommand::CDUP}, {"MKD", FtpCommand::MKD},
        {"RMD", FtpCommand::RMD},   {"LIST", FtpCommand::LIST},
        {"NLST", FtpCommand::NLST}, {"STAT", FtpCommand::STAT},
        {"SIZE", FtpCommand::SIZE}, {"MDTM", FtpCommand::MDTM},
        {"TYPE", FtpCommand::TYPE}, {"MODE", FtpCommand::MODE},
        {"PORT", FtpCommand::PORT}, {"PASV", FtpCommand::PASV},
        {"RETR", FtpCommand::RETR}, {"STOR", FtpCommand::STOR},
        {"STOU", FtpCommand::STOU}, {"APPE", FtpCommand::APPE},
        {"DELE", FtpCommand::DELE}, {"RNFR", FtpCommand::RNFR},
        {"RNTO", FtpCommand::RNTO}, {"HASH", FtpCommand::HASH},
        {"ABOR", FtpCommand::ABOR}, {"HELP", FtpCommand::HELP}};

    const auto iterator = table.find(verb);
    return iterator != table.end() ? iterator->second : FtpCommand::UNKNOWN;
}

struct ParsedCommand
{
    FtpCommand cmd = FtpCommand::UNKNOWN;
    std::string verbRaw;
    std::string arg;
};

inline ParsedCommand parseLine(const std::string &line)
{
    ParsedCommand result;
    std::string trimmed = line;

    while (!trimmed.empty() &&
           (trimmed.back() == '\r' || trimmed.back() == '\n'))
    {
        trimmed.pop_back();
    }

    std::istringstream input(trimmed);
    std::string verb;
    input >> verb;

    for (char &character : verb)
    {
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
    }

    std::string arg;
    std::getline(input, arg);
    if (!arg.empty() && arg.front() == ' ')
    {
        arg.erase(0, 1);
    }

    result.verbRaw = verb;
    result.arg = arg;
    result.cmd = parseCommandVerb(verb);
    return result;
}

enum class TransferType
{
    ASCII,
    BINARY
};

enum class TransferMode
{
    STREAM,
    BLOCK,
    COMPRESSED
};

enum class DataConnMode
{
    NONE,
    ACTIVE,
    PASSIVE
};

struct ClientSession
{
    int controlSocketFd = -1;
    int sessionId = 0;
    std::string clientIp;

    bool authenticated = false;
    std::string username;

    std::string currentDir = "/";
    TransferType type = TransferType::ASCII;
    TransferMode mode = TransferMode::STREAM;

    DataConnMode dataMode = DataConnMode::NONE;
    std::string dataIp;
    int dataPort = 0;
    int pasvListenFd = -1;
    int dataSocketFd = -1;

    std::string renameFrom;

    std::atomic<bool> transferActive{false};
    std::atomic<bool> abortRequested{false};
};

#endif // PROTOCOL_H