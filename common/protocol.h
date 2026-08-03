#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>

// Mã phản hồi chuẩn FTP (Standard FTP Reply Codes)
namespace FTPStatus {
    const std::string OK_150 = "150 File status okay; opening data connection.\r\n";
    const std::string OK_200 = "200 Command OK.\r\n";
    const std::string OK_220 = "220 Service ready for new user.\r\n";
    const std::string OK_221 = "221 Goodbye.\r\n";
    const std::string OK_226 = "226 Closing data connection. Requested file action successful.\r\n";
    const std::string OK_230 = "230 User logged in, proceed.\r\n";
    const std::string NEED_PASS_331 = "331 Username OK, need password.\r\n";
    const std::string ERR_425 = "425 Can't open data connection.\r\n";
    const std::string ERR_500 = "500 Syntax error, command unrecognized.\r\n";
    const std::string ERR_530 = "530 Not logged in.\r\n";
    const std::string ERR_550 = "550 File unavailable.\r\n";
}

#endif // PROTOCOL_H