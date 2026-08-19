#include <atomic>
#include <chrono>
#include <ctime>
#include <cctype>
#include <iostream>
#include <memory>
#include <mutex>
#include <cstring>
#include <string>
#include <filesystem>
#include <optional>
#include <random>
#include <thread>
#include <fstream>
#include <unordered_map>
#include "authentication_manager.h"
#include "session_registry.h"
#include "../common/sha256.h"
#include "../common/logger.h"
#include "../common/protocol.h"
#include "../common/socket_platform.h"
#include "../network/data_channel.h"
#include "../network/data_channel_config.h"

#define SERVER_PORT 2121
#define BUFFER_SIZE 1024

namespace fs = std::filesystem;
// chứa đường dẫn tuyệt đối đến thư mục ftp_root
const fs::path SERVER_ROOT = fs::absolute("ftp_root");

// New feature modules. They are shared by all client threads.
AuthenticationManager g_authenticationManager;
SessionRegistry g_sessionRegistry;
std::mutex g_controlSendMutex;
std::atomic<int> g_nextSessionId{1};

bool sendAll(SOCKET socketFd, const std::string &data)
{
    std::size_t totalSent = 0;

    while (totalSent < data.size())
    {
        const int sent = send(
            socketFd,
            data.data() + totalSent,
            static_cast<int>(data.size() - totalSent),
            0);

        if (sent == SOCKET_ERROR || sent <= 0)
        {
            return false;
        }

        totalSent += static_cast<std::size_t>(sent);
    }

    return true;
}

fs::path sessionTransferDirectory(const ClientSession &session)
{
    return fs::temp_directory_path()
        / "hybridftp_sessions"
        / ("session_" + std::to_string(session.sessionId));
}

void resetSessionTransferDirectory(const ClientSession &session)
{
    const fs::path dir = sessionTransferDirectory(session);
    std::error_code ec;
    fs::remove_all(dir, ec);
    ec.clear();
    fs::create_directories(dir, ec);
}

void cleanupSessionTransferDirectory(const ClientSession &session)
{
    std::error_code ec;
    fs::remove_all(sessionTransferDirectory(session), ec);
}

// Hàm khởi tạo Socket
bool init_sockets()
{
    return
        SocketPlatform::initialize();
}

// Hàm dọn dẹp Socket khi đóng chương trình
void cleanup_sockets()
{
    SocketPlatform::cleanup();
}

void send_reply(SOCKET client_socket, const std::string &reply)
{
    std::lock_guard<std::mutex> lock(g_controlSendMutex);

    if (!sendAll(client_socket, reply))
    {
        log_error("Failed to send FTP reply. OS error="
                  + std::to_string(SocketPlatform::lastError()));
    }
}

std::optional<fs::path> resolveVirtualPath(
    const ClientSession &session,
    const std::string &arg)
{
    fs::path virtualPath;

    if (arg.empty())
    {
        virtualPath = fs::path(session.currentDir);
    }
    else if (arg.front() == '/')
    {
        virtualPath = fs::path(arg);
    }
    else
    {
        virtualPath = fs::path(session.currentDir) / arg;
    }

    const fs::path combined =
        (SERVER_ROOT / virtualPath.relative_path()).lexically_normal();

    const fs::path relative = combined.lexically_relative(SERVER_ROOT);

    if (relative.empty())
    {
        return std::nullopt;
    }

    for (const auto &component : relative)
    {
        if (component == "..")
        {
            return std::nullopt;
        }
    }

    return combined;
}

// Chuyển 1 đường dẫn THẬT trên đĩa (đã nằm trong SERVER_ROOT) ngược lại
// thành đường dẫn "ảo" để hiển thị / lưu vào session.currentDir.
std::string toVirtualPath(const fs::path &realPath)
{
    fs::path rel = fs::relative(realPath, SERVER_ROOT); // tạo đường dẫn tương đối từ SERVER_ROOT đến realPat
    if (rel == ".")
        return "/"; // dang o dung thu muc goc
    return "/" + rel.generic_string();
}

// Chuyển quyền truy cập của file/thư mục thành chuỗi rwxrwxrwx.
// Ví dụ: rw-r--r--, rwxr-xr-x.
std::string getPermissionString(const fs::path &path)
{
    std::error_code ec;

    fs::perms permissions =
        fs::status(path, ec).permissions();

    if (ec)
    {
        return "---------";
    }

    std::string result;

    result +=
        (permissions & fs::perms::owner_read) != fs::perms::none
            ? 'r'
            : '-';

    result +=
        (permissions & fs::perms::owner_write) != fs::perms::none
            ? 'w'
            : '-';

    result +=
        (permissions & fs::perms::owner_exec) != fs::perms::none
            ? 'x'
            : '-';

    result +=
        (permissions & fs::perms::group_read) != fs::perms::none
            ? 'r'
            : '-';

    result +=
        (permissions & fs::perms::group_write) != fs::perms::none
            ? 'w'
            : '-';

    result +=
        (permissions & fs::perms::group_exec) != fs::perms::none
            ? 'x'
            : '-';

    result +=
        (permissions & fs::perms::others_read) != fs::perms::none
            ? 'r'
            : '-';

    result +=
        (permissions & fs::perms::others_write) != fs::perms::none
            ? 'w'
            : '-';

    result +=
        (permissions & fs::perms::others_exec) != fs::perms::none
            ? 'x'
            : '-';

    return result;
}

// Tạo một dòng chi tiết cho lệnh LIST.
// Format:
// <type> <permissions> <size> <name>
//
// type:
// d = directory
// - = regular file
// ? = loại khác
std::string buildListEntry(
    const fs::directory_entry &entry)
{
    char type = '?';

    if (entry.is_directory())
    {
        type = 'd';
    }
    else if (entry.is_regular_file())
    {
        type = '-';
    }

    std::string sizeText = "-";

    if (entry.is_regular_file())
    {
        std::error_code ec;

        std::uintmax_t fileSize =
            entry.file_size(ec);

        if (!ec)
        {
            sizeText =
                std::to_string(fileSize);
        }
    }

    return std::string(1, type) + " " + getPermissionString(entry.path()) + " " + sizeText + " " + entry.path().filename().string();
}

// Helper cho PORT / PASV: đọc "h1,h2,h3,h4,p1,p2" -> IP + port
// Đây là định dạng chuẩn FTP: p_port = p1*256 + p2
bool parsePortArg(const std::string &arg, std::string &outIp, int &outPort)
{
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(arg.c_str(), "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6)
        return false;
    outIp = std::to_string(h1) + "." + std::to_string(h2) + "." +
            std::to_string(h3) + "." + std::to_string(h4);
    outPort = p1 * 256 + p2; // FTP biểu diễn port bằng 2 byte
    return true;
}

// Lấy IP local của server (theo góc nhìn của kết nối TCP hiện tại) để trả
// về trong reply PASV — dùng getsockname() thay vì hardcode IP.
std::string getLocalIp(SOCKET client_socket)
{
    sockaddr_in localAddr{};
    SocketPlatform::Length len = sizeof(localAddr);
    getsockname(client_socket, (struct sockaddr *)&localAddr, &len); // lấy địa chỉ IP và port mà socket client_socket đang sử dụng ở phía server.
    char ipBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &localAddr.sin_addr, ipBuf, sizeof(ipBuf)); // chuyển địa chỉ IP nhị phân (localAddr.sin_addr) thành chuỗi dạng "x.x.x.x".
    return std::string(ipBuf);
}

// Dựng chuỗi reply 227 đúng định dạng FTP: "h1,h2,h3,h4,p1,p2"
std::string formatPasvReply(const std::string &ip, int port)
{
    int h1, h2, h3, h4;
    sscanf(ip.c_str(), "%d.%d.%d.%d", &h1, &h2, &h3, &h4);
    int p1 = port / 256; // byte cao
    int p2 = port % 256; // byte thấp
    return "227 Entering Passive Mode (" + std::to_string(h1) + "," + std::to_string(h2) + "," +
           std::to_string(h3) + "," + std::to_string(h4) + "," + std::to_string(p1) + "," +
           std::to_string(p2) + ").\r\n";
}

std::string buildHelpReply(const std::string &commandArgument)
{
    if (commandArgument.empty())
    {
        return "214-The following commands are recognized:\r\n"
               " USER PASS QUIT NOOP PWD CWD CDUP MKD RMD\r\n"
               " LIST NLST STAT SIZE MDTM TYPE MODE PORT PASV\r\n"
               " RETR STOR STOU APPE DELE RNFR RNTO HASH ABOR HELP\r\n"
               "214 Help OK.\r\n";
    }

    std::string command = commandArgument;
    for (char &character : command)
    {
        character = static_cast<char>(std::toupper(character));
    }

    static const std::unordered_map<std::string, std::string> helpTable = {
        {"USER", "USER <username> - Send username for authentication."},
        {"PASS", "PASS <password> - Send password to complete login."},
        {"QUIT", "QUIT - Close the FTP session."},
        {"NOOP", "NOOP - Keep the control connection alive."},
        {"PWD", "PWD - Show the current server directory."},
        {"CWD", "CWD <path> - Change the current directory."},
        {"CDUP", "CDUP - Move to the parent directory."},
        {"MKD", "MKD <dirname> - Create a directory."},
        {"RMD", "RMD <dirname> - Remove an empty directory."},
        {"LIST", "LIST [path] - Detailed directory listing."},
        {"NLST", "NLST [path] - Name-only directory listing."},
        {"STAT", "STAT [path] - Show server/session or path status."},
        {"SIZE", "SIZE <filename> - Return file size in bytes."},
        {"MDTM", "MDTM <filename> - Return last modification time."},
        {"TYPE", "TYPE A|I - Select ASCII or binary transfer type."},
        {"MODE", "MODE S|B|C - Select transfer mode."},
        {"PORT", "PORT h1,h2,h3,h4,p1,p2 - Select Active mode."},
        {"PASV", "PASV - Select Passive mode."},
        {"RETR", "RETR <filename> - Download a file."},
        {"STOR", "STOR <filename> - Upload a file."},
        {"STOU", "STOU - Upload using a unique server-generated name."},
        {"APPE", "APPE <filename> - Append uploaded data to a file."},
        {"DELE", "DELE <filename> - Delete a file."},
        {"RNFR", "RNFR <oldname> - Start rename operation."},
        {"RNTO", "RNTO <newname> - Finish rename operation."},
        {"HASH", "HASH <filename> - Return SHA-256 of a server file."},
        {"ABOR", "ABOR - Request cancellation of the current transfer."},
        {"HELP", "HELP [command] - Show command usage."}};

    const auto iterator = helpTable.find(command);
    if (iterator == helpTable.end())
    {
        return "501 Unknown HELP command.\r\n";
    }

    return "214 " + iterator->second + "\r\n";
}
// Chuyển dòng sang CRLF (\r\n) khi gửi ở chế độ ASCII
std::string convertToCRLF(const std::string &buffer)
{
    std::string result;
    result.reserve(buffer.size() * 1.1);
    for (size_t i = 0; i < buffer.size(); ++i)
    {
        if (buffer[i] == '\n' && (i == 0 || buffer[i - 1] != '\r'))
        {
            result += "\r\n";
        }
        else
        {
            result += buffer[i];
        }
    }
    return result;
}

// chuyênr đổi dòng CRLF (\r\n) sang LF (\n) khi nhận ở chế độ ASCII
std::string convertFromCRLF(const std::string &buffer)
{
    std::string result;
    result.reserve(buffer.size());
    for (size_t i = 0; i < buffer.size(); ++i)
    {
        if (buffer[i] == '\r' && i + 1 < buffer.size() && buffer[i + 1] == '\n')
        {
            result += '\n';
            ++i;
        }
        else
        {
            result += buffer[i];
        }
    }
    return result;
}
void updateTransferState(ClientSession &session, bool transferActive)
{
    session.transferActive.store(transferActive);

    if (transferActive)
    {
        session.abortRequested.store(false);
    }
    g_sessionRegistry.setTransferActive(session.sessionId, transferActive);
    g_sessionRegistry.printSessions();
}

static bool convertCRLFToLFInFile(const fs::path &srcPath, const fs::path &dstPath)
{
    std::ifstream in(srcPath, std::ios::binary);
    std::ofstream out(dstPath, std::ios::binary);
    if (!in.is_open() || !out.is_open())
        return false;

    char c;
    while (in.get(c))
    {
        if (c == '\r')
        {
            if (in.peek() == '\n')
            {
                in.get(c); // Bỏ qua '\r', lấy ký tự '\n' tiếp theo để ghi
            }
        }
        out.put(c);
    }
    return true;
}

std::string commandForLog(const ParsedCommand &parsed)
{
    if (parsed.cmd == FtpCommand::PASS)
    {
        return "PASS ********";
    }

    std::string command = parsed.verbRaw;

    if (!parsed.arg.empty())
    {
        command += " " + parsed.arg;
    }

    return command;
}

// Hàm xử lý phân tích và phản hồi lệnh FTP
void handle_client_command(
    SOCKET client_socket,
    const std::shared_ptr<ClientSession> &sessionPtr,
    const std::shared_ptr<DataChannel> &dataChannelPtr,
    const std::string &raw_line)
{
    ClientSession &session = *sessionPtr;
    DataChannel &dataChannel = *dataChannelPtr;

    ParsedCommand parsed = parseLine(raw_line);

    if (session.transferActive.load() &&
        parsed.cmd != FtpCommand::ABOR)
    {
        send_reply(
            client_socket,
            "450 Transfer in progress; only ABOR is allowed.\r\n");
        return;
    }

    switch (parsed.cmd)
    {
    case FtpCommand::USER:
    {
        if (parsed.arg.empty())
        {
            send_reply(client_socket, FTPStatus::ERR_501);
            break;
        }

        if (!g_authenticationManager.userExists(parsed.arg))
        {
            session.username.clear();
            session.authenticated = false;
            g_sessionRegistry.updateAuthentication(session.sessionId, "", false);
            send_reply(client_socket, "530 User not found.\r\n");
            break;
        }

        session.username = parsed.arg;
        session.authenticated = false;
        g_sessionRegistry.updateAuthentication(session.sessionId, session.username, false);
        g_sessionRegistry.printSessions();

        log_info("Client set username: " + session.username);
        send_reply(client_socket, FTPStatus::NEED_PASS_331);
        break;
    }
    case FtpCommand::PASS:
    {
        if (session.username.empty() ||
            !g_authenticationManager.validateCredentials(session.username, parsed.arg))
        {
            session.authenticated = false;
            g_sessionRegistry.updateAuthentication(session.sessionId, session.username, false);
            send_reply(client_socket, "530 Login incorrect.\r\n");
            break;
        }

        session.authenticated = true;
        g_sessionRegistry.updateAuthentication(session.sessionId, session.username, true);
        g_sessionRegistry.printSessions();

        log_info("User '" + session.username + "' authenticated.");
        send_reply(client_socket, FTPStatus::OK_230);
        break;
    }
    case FtpCommand::NOOP:
    {
        send_reply(client_socket, FTPStatus::OK_200);
        break;
    }
    case FtpCommand::QUIT:
    {
        send_reply(client_socket, FTPStatus::OK_221);
        break;
    }

    // ---------------- Từ đây trở xuống: bắt buộc phải login ----------------
    default:
    {
        if (!session.authenticated)
        {
            send_reply(client_socket, FTPStatus::ERR_530);
            break;
        }

        switch (parsed.cmd)
        {
        case FtpCommand::PWD:
        {
            send_reply(client_socket, "257 \"" + session.currentDir + "\" is current directory.\r\n");
            break;
        }

        case FtpCommand::CWD:
        {
            auto target = resolveVirtualPath(session, parsed.arg);
            if (!target || !fs::is_directory(*target))
            {
                send_reply(client_socket, FTPStatus::ERR_550);
            }
            else
            {
                session.currentDir = toVirtualPath(*target);
                send_reply(client_socket, FTPStatus::OK_250);
            }
            break;
        }
        case FtpCommand::CDUP:
        {
            // Nếu đang ở thư mục gốc
            // thì không đi lên nữa.
            if (session.currentDir == "/")
            {
                send_reply(
                    client_socket,
                    FTPStatus::OK_250);

                break;
            }

            auto target =
                resolveVirtualPath(
                    session,
                    "..");

            if (!target ||
                !fs::is_directory(*target))
            {
                send_reply(
                    client_socket,
                    FTPStatus::ERR_550);

                break;
            }

            session.currentDir =
                toVirtualPath(*target);

            send_reply(
                client_socket,
                FTPStatus::OK_250);

            break;
        }
        case FtpCommand::MKD:
        {
            auto target = resolveVirtualPath(session, parsed.arg);
            if (!target || parsed.arg.empty())
            {
                send_reply(client_socket, FTPStatus::ERR_501);
            }
            else if (fs::exists(*target))
            {
                send_reply(client_socket, FTPStatus::ERR_550);
            }
            else
            {
                std::error_code ec;
                fs::create_directory(*target, ec);
                if (ec)
                    send_reply(client_socket, FTPStatus::ERR_550);
                else
                    send_reply(client_socket, "257 \"" + toVirtualPath(*target) + "\" directory created.\r\n");
            }
            break;
        }

        case FtpCommand::RMD:
        {
            auto target = resolveVirtualPath(session, parsed.arg);
            if (!target || !fs::is_directory(*target))
            {
                send_reply(client_socket, FTPStatus::ERR_550);
            }
            else
            {
                std::error_code ec;
                fs::remove(*target, ec);
                if (ec)
                    send_reply(client_socket, FTPStatus::ERR_550);
                else
                    send_reply(client_socket, FTPStatus::OK_250);
            }
            break;
        }

        case FtpCommand::DELE:
        {
            auto target = resolveVirtualPath(session, parsed.arg);
            if (!target || !fs::is_regular_file(*target))
            {
                send_reply(client_socket, FTPStatus::ERR_550);
            }
            else
            {
                std::error_code ec;
                fs::remove(*target, ec);
                if (ec)
                    send_reply(client_socket, FTPStatus::ERR_550);
                else
                    send_reply(client_socket, FTPStatus::OK_250);
            }
            break;
        }

        case FtpCommand::RNFR:
        {
            auto target = resolveVirtualPath(session, parsed.arg);
            if (!target || !fs::exists(*target))
            {
                send_reply(client_socket, FTPStatus::ERR_550);
            }
            else
            {
                session.renameFrom = parsed.arg;
                send_reply(client_socket, FTPStatus::PENDING_350);
            }
            break;
        }

        case FtpCommand::RNTO:
        {
            if (session.renameFrom.empty())
            {
                send_reply(client_socket, FTPStatus::ERR_501); // chưa gọi RNFR trước
                break;
            }
            auto from = resolveVirtualPath(session, session.renameFrom);
            auto to = resolveVirtualPath(session, parsed.arg);
            session.renameFrom.clear(); // dùng 1 lần rồi xoá, dù thành công hay không
            if (!from || !to || !fs::exists(*from))
            {
                send_reply(client_socket, FTPStatus::ERR_550);
            }
            else
            {
                std::error_code ec;
                fs::rename(*from, *to, ec);
                if (ec)
                    send_reply(client_socket, FTPStatus::ERR_550);
                else
                    send_reply(client_socket, FTPStatus::OK_250);
            }
            break;
        }

        case FtpCommand::TYPE:
        {
            std::string t = parsed.arg;
            for (char &c : t)
            {
                c = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c)));
            }
            if (t == "A")
            {
                session.type = TransferType::ASCII;
                send_reply(client_socket, "200 Type set to A.\r\n");
            }
            else if (t == "I")
            {
                session.type = TransferType::BINARY;
                send_reply(client_socket, "200 Type set to I.\r\n");
            }
            else
            {
                send_reply(client_socket, FTPStatus::ERR_501);
            }
            break;
        }

        case FtpCommand::SIZE:
        {
            auto target = resolveVirtualPath(session, parsed.arg);
            if (!target || !fs::is_regular_file(*target))
            {
                send_reply(client_socket, FTPStatus::ERR_550);
            }
            else
            {
                std::error_code ec;
                auto sz = fs::file_size(*target, ec);
                if (ec)
                    send_reply(client_socket, FTPStatus::ERR_550);
                else
                    send_reply(client_socket, "213 " + std::to_string(sz) + "\r\n");
            }
            break;
        }

        case FtpCommand::PORT:
        {
            std::string ip;
            int port;
            if (!parsePortArg(parsed.arg, ip, port))
            {
                send_reply(client_socket, FTPStatus::ERR_501);
                break;
            }

            // Nếu trước đó đã mở kênh (ví dụ client gọi lại PORT lần 2)
            // phải đóng kênh cũ trước khi mở kênh mới -- DataChannel::open()
            // sẽ KHÔNG làm gì nếu thấy đã "opened", nên phải tự đóng tay.
            if (dataChannel.isOpened())
                dataChannel.close();

            DataChannelConfig cfg;
            cfg.localPort = 0; // ACTIVE mode: để hệ điều hành tự chọn port cho server
            cfg.timeout = 2000;
            cfg.maxRetry = 5;
            cfg.useGBN = true;
            cfg.simulateAckLoss = false;

            if (!dataChannel.open(cfg))
            {
                send_reply(client_socket, FTPStatus::ERR_425);
                break;
            }

            // Ghi lại số hiệu socket THẬT đang dùng vào session -- để
            // chứng minh mỗi session sở hữu 1 tài nguyên OS hoàn toàn
            // riêng biệt (phục vụ "fully isolated session"), có thể xem
            // qua STAT khi có nhiều client kết nối cùng lúc.
            session.dataSocketFd = dataChannel.getSocketFd();
            session.pasvListenFd = -1; // không áp dụng cho ACTIVE mode

            session.dataMode = DataConnMode::ACTIVE;
            session.dataIp = ip;
            session.dataPort = port;

            log_info("Client requested ACTIVE mode -> " + ip + ":" + std::to_string(port));
            send_reply(client_socket, FTPStatus::OK_200);
            break;
        }

        case FtpCommand::PASV:
        {
            thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> distPort(50000, 51000);
            std::string ip = getLocalIp(client_socket);

            if (dataChannel.isOpened())
            {
                dataChannel.close();
                session.dataSocketFd = -1;
                session.pasvListenFd = -1;
            }
            bool opened = false;
            int chosenPort = 0;
            // Thử tối đa 10 lần vì có thể trùng port với client khác
            // đang chạy song song (server đa luồng) -- xác suất rất
            // thấp nhưng vẫn cần xử lý thay vì để treo/lỗi im lặng.
            for (int attempt = 0; attempt < 10 && !opened; attempt++)
            {
                chosenPort = distPort(rng);
                DataChannelConfig cfg;
                cfg.localPort = static_cast<unsigned short>(chosenPort);
                cfg.timeout = 2000;
                cfg.maxRetry = 5;
                cfg.simulateAckLoss = false;
                cfg.useGBN = true;
                opened = dataChannel.open(cfg);
            }

            if (!opened)
            {
                log_error("PASV: could not bind any UDP port after 10 attempts.");
                send_reply(client_socket, FTPStatus::ERR_425);
                break;
            }

            // Cùng 1 fd -- PASV chỉ có 1 socket vừa lắng nghe vừa truyền
            // dữ liệu (khác TCP, không có accept() riêng), nhưng lưu vào
            // 2 field tên khác nhau để phản ánh đúng VAI TRÒ trong session.
            session.dataSocketFd = dataChannel.getSocketFd();
            session.pasvListenFd = session.dataSocketFd;

            session.dataMode = DataConnMode::PASSIVE;
            session.dataIp = ip;
            session.dataPort = chosenPort;
            log_info("Server entering PASSIVE mode -> " + ip + ":" + std::to_string(chosenPort));
            send_reply(client_socket, formatPasvReply(ip, chosenPort));
            break;
        }

        case FtpCommand::STOR:
        {
            if (session.dataMode == DataConnMode::NONE || !dataChannel.isOpened())
            {
                send_reply(client_socket, FTPStatus::ERR_425);
                break;
            }

            if (parsed.arg.empty())
            {
                send_reply(client_socket, FTPStatus::ERR_501);
                break;
            }

            resetSessionTransferDirectory(session);
            const fs::path tempDir = sessionTransferDirectory(session);
            const std::string targetName = parsed.arg;

            send_reply(client_socket, FTPStatus::OK_150);
            updateTransferState(session, true);

            std::thread(
                [client_socket, sessionPtr, dataChannelPtr, tempDir, targetName]()
                {
                    ClientSession &threadSession = *sessionPtr;
                    DataChannel &threadChannel = *dataChannelPtr;

                    const bool ok = threadChannel.receiveFile(tempDir.string());

                    if (!ok)
                    {
                        const bool aborted = threadSession.abortRequested.load();

                        if (!threadChannel.isBusy())
                        {
                            threadChannel.close();
                        }
                        threadSession.dataMode = DataConnMode::NONE;
                        cleanupSessionTransferDirectory(threadSession);
                        updateTransferState(threadSession, false);

                        if (!aborted)
                        {
                            send_reply(client_socket, FTPStatus::ERR_426);
                        }
                        return;
                    }

                    const TransferSession &recvInfo =
                        threadChannel.getReceiveTransferSession();
                    const fs::path receivedPath = tempDir / recvInfo.fileName;
                    const auto destination =
                        resolveVirtualPath(threadSession, targetName);

                    bool stored = destination.has_value() && fs::exists(receivedPath);
                    std::error_code ec;

                    if (stored && threadSession.type == TransferType::ASCII)
                    {
                        stored = convertCRLFToLFInFile(receivedPath, *destination);
                    }
                    else if (stored)
                    {
                        if (fs::exists(*destination))
                        {
                            fs::remove(*destination, ec);
                            ec.clear();
                        }

                        fs::rename(receivedPath, *destination, ec);
                        stored = !ec;
                    }

                    cleanupSessionTransferDirectory(threadSession);
                    threadChannel.close();
                    threadSession.dataMode = DataConnMode::NONE;
                    updateTransferState(threadSession, false);

                    if (threadSession.abortRequested.load())
                    {
                        return;
                    }

                    send_reply(
                        client_socket,
                        stored ? FTPStatus::OK_226 : FTPStatus::ERR_550);
                })
                .detach();

            break;
        }

    case FtpCommand::RETR:
    {
        if (session.dataMode == DataConnMode::NONE || !dataChannel.isOpened())
        {
            send_reply(client_socket, FTPStatus::ERR_425);
            break;
        }

        auto target = resolveVirtualPath(session, parsed.arg);
        if (!target || !fs::is_regular_file(*target))
        {
            send_reply(client_socket, FTPStatus::ERR_550);
            break;
        }

        std::string destIp = session.dataIp;
        unsigned short destPort = static_cast<unsigned short>(session.dataPort);

        if (session.dataMode == DataConnMode::PASSIVE)
        {
            send_reply(client_socket, FTPStatus::OK_150);

            std::string learnedIp;
            unsigned short learnedPort = 0;

            if (!dataChannel.receiveHandshake(learnedIp, learnedPort))
            {
                send_reply(client_socket, FTPStatus::ERR_425);
                break;
            }

            destIp = learnedIp;
            destPort = learnedPort;
        }
        else
        {
            send_reply(client_socket, FTPStatus::OK_150);
        }

        const std::string targetPath = target->string();
        const TransferType transferType = session.type;

        updateTransferState(session, true);

        std::thread(
            [client_socket,
             sessionPtr,
             dataChannelPtr,
             targetPath,
             destIp,
             destPort,
             transferType]()
            {
                ClientSession &threadSession = *sessionPtr;
                DataChannel &threadChannel = *dataChannelPtr;

                const bool ok = threadChannel.sendFile(
                    targetPath,
                    destIp,
                    destPort,
                    transferType);

                const bool aborted = threadSession.abortRequested.load();

                threadChannel.close();
                threadSession.dataMode = DataConnMode::NONE;
                updateTransferState(threadSession, false);

                if (!aborted)
                {
                    send_reply(
                        client_socket,
                        ok ? FTPStatus::OK_226 : FTPStatus::ERR_426);
                }
            })
            .detach();

        break;
    }
    case FtpCommand::LIST:
    {
        if (session.dataMode == DataConnMode::NONE || !dataChannel.isOpened())
        {
            send_reply(client_socket, FTPStatus::ERR_425);
            break;
        }

        auto dirPath = resolveVirtualPath(session, parsed.arg);
        if (!dirPath || !fs::is_directory(*dirPath))
        {
            send_reply(client_socket, FTPStatus::ERR_550);
            break;
        }

        // Ghi danh sách thư mục ra 1 file tạm, rồi gửi file tạm đó qua
        // DataChannel giống hệt cách RETR gửi 1 file bình thường --
        // tái dùng đúng logic FileSender đã test kỹ, không viết lại.
        fs::path tempListing = fs::temp_directory_path() /
                               ("list_" + std::to_string(session.controlSocketFd) + ".txt");
        {
            std::ofstream listFile(tempListing);

            for (const auto &entry :
                 fs::directory_iterator(*dirPath))
            {
                listFile
                    << buildListEntry(entry)
                    << "\n";
            }
        }

        std::string destIp = session.dataIp;
        unsigned short destPort = static_cast<unsigned short>(session.dataPort);

        // Hỗ trợ PASV cho LIST bằng cách nhận Handshake từ Client
        if (session.dataMode == DataConnMode::PASSIVE)
        {
            send_reply(client_socket, FTPStatus::OK_150);
            std::string learnedIp;
            unsigned short learnedPort;
            if (!dataChannel.receiveHandshake(learnedIp, learnedPort))
            {
                std::error_code ec;
                fs::remove(tempListing, ec);
                send_reply(client_socket, FTPStatus::ERR_425);
                break;
            }
            destIp = learnedIp;
            destPort = learnedPort;
        }
        else
        {
            send_reply(client_socket, FTPStatus::OK_150);
        }

        updateTransferState(session, true);
        bool ok = dataChannel.sendFile(tempListing.string(), destIp, destPort);
        updateTransferState(session, false);

        std::error_code ec;
        fs::remove(tempListing, ec);

        dataChannel.close();
        session.dataMode = DataConnMode::NONE;

        send_reply(client_socket, ok ? FTPStatus::OK_226 : FTPStatus::ERR_426);
        break;
    }

    case FtpCommand::NLST:
    {
        if (session.dataMode == DataConnMode::NONE || !dataChannel.isOpened())
        {
            send_reply(client_socket, FTPStatus::ERR_425);
            break;
        }

        auto dirPath = resolveVirtualPath(session, parsed.arg);
        if (!dirPath || !fs::is_directory(*dirPath))
        {
            send_reply(client_socket, FTPStatus::ERR_550);
            break;
        }

        // LIST: "d ten" / "- ten" (kieu giong 'ls -l' rut gon)
        // NLST: chi ten file/thu muc, moi dong 1 ten (kieu 'ls' tron)
        bool isNlst = (parsed.cmd == FtpCommand::NLST);
        fs::path tempListing = fs::temp_directory_path() /
                               ("list_" + std::to_string(session.controlSocketFd) + ".txt");
        {
            std::ofstream listFile(tempListing);
            for (auto &entry : fs::directory_iterator(*dirPath))
            {
                if (isNlst)
                {
                    listFile << entry.path().filename().string() << "\n";
                }
                else
                {
                    listFile << (entry.is_directory() ? "d " : "- ")
                             << entry.path().filename().string() << "\n";
                }
            }
        }

        std::string destIp = session.dataIp;
        unsigned short destPort = static_cast<unsigned short>(session.dataPort);

        if (session.dataMode == DataConnMode::PASSIVE)
        {
            send_reply(client_socket, FTPStatus::OK_150);
            std::string learnedIp;
            unsigned short learnedPort;
            if (!dataChannel.receiveHandshake(learnedIp, learnedPort))
            {
                std::error_code ec;
                fs::remove(tempListing, ec);
                send_reply(client_socket, FTPStatus::ERR_425);
                break;
            }
            destIp = learnedIp;
            destPort = learnedPort;
        }
        else
        {
            send_reply(client_socket, FTPStatus::OK_150);
        }

        updateTransferState(session, true);
        bool ok = dataChannel.sendFile(tempListing.string(), destIp, destPort);
        updateTransferState(session, false);

        std::error_code ec;
        fs::remove(tempListing, ec);

        dataChannel.close();
        session.dataMode = DataConnMode::NONE;

        send_reply(client_socket, ok ? FTPStatus::OK_226 : FTPStatus::ERR_426);
        break;
    }
    case FtpCommand::STOU:
    {
        if (session.dataMode == DataConnMode::NONE || !dataChannel.isOpened())
        {
            send_reply(client_socket, FTPStatus::ERR_425);
            break;
        }

        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        const std::string uniqueName =
            "upload_" + std::to_string(session.sessionId)
            + "_" + std::to_string(nowMs);

        resetSessionTransferDirectory(session);
        const fs::path tempDir = sessionTransferDirectory(session);

        send_reply(client_socket, FTPStatus::OK_150);
        updateTransferState(session, true);

        std::thread(
            [client_socket, sessionPtr, dataChannelPtr, tempDir, uniqueName]()
            {
                ClientSession &threadSession = *sessionPtr;
                DataChannel &threadChannel = *dataChannelPtr;

                const bool ok = threadChannel.receiveFile(tempDir.string());

                if (!ok)
                {
                    const bool aborted = threadSession.abortRequested.load();
                    threadChannel.close();
                    threadSession.dataMode = DataConnMode::NONE;
                    cleanupSessionTransferDirectory(threadSession);
                    updateTransferState(threadSession, false);

                    if (!aborted)
                    {
                        send_reply(client_socket, FTPStatus::ERR_426);
                    }
                    return;
                }

                const TransferSession &recvInfo =
                    threadChannel.getReceiveTransferSession();
                const fs::path receivedPath = tempDir / recvInfo.fileName;
                const auto destination =
                    resolveVirtualPath(threadSession, uniqueName);

                bool stored = destination.has_value() && fs::exists(receivedPath);
                std::error_code ec;

                if (stored && threadSession.type == TransferType::ASCII)
                {
                    stored = convertCRLFToLFInFile(receivedPath, *destination);
                }
                else if (stored)
                {
                    fs::rename(receivedPath, *destination, ec);
                    stored = !ec;
                }

                cleanupSessionTransferDirectory(threadSession);
                threadChannel.close();
                threadSession.dataMode = DataConnMode::NONE;
                updateTransferState(threadSession, false);

                if (threadSession.abortRequested.load())
                {
                    return;
                }

                if (stored)
                {
                    send_reply(
                        client_socket,
                        "226 Transfer complete. Stored as \""
                        + uniqueName
                        + "\".\r\n");
                }
                else
                {
                    send_reply(client_socket, FTPStatus::ERR_550);
                }
            })
            .detach();

        break;
    }

    case FtpCommand::APPE:
    {
        if (session.dataMode == DataConnMode::NONE || !dataChannel.isOpened())
        {
            send_reply(client_socket, FTPStatus::ERR_425);
            break;
        }

        if (parsed.arg.empty())
        {
            send_reply(client_socket, FTPStatus::ERR_501);
            break;
        }

        resetSessionTransferDirectory(session);
        const fs::path tempDir = sessionTransferDirectory(session);
        const std::string targetName = parsed.arg;

        send_reply(client_socket, FTPStatus::OK_150);
        updateTransferState(session, true);

        std::thread(
            [client_socket, sessionPtr, dataChannelPtr, tempDir, targetName]()
            {
                ClientSession &threadSession = *sessionPtr;
                DataChannel &threadChannel = *dataChannelPtr;

                const bool ok = threadChannel.receiveFile(tempDir.string());

                if (!ok)
                {
                    const bool aborted = threadSession.abortRequested.load();
                    threadChannel.close();
                    threadSession.dataMode = DataConnMode::NONE;
                    cleanupSessionTransferDirectory(threadSession);
                    updateTransferState(threadSession, false);

                    if (!aborted)
                    {
                        send_reply(client_socket, FTPStatus::ERR_426);
                    }
                    return;
                }

                const TransferSession &recvInfo =
                    threadChannel.getReceiveTransferSession();
                const fs::path receivedPath = tempDir / recvInfo.fileName;
                const auto destination =
                    resolveVirtualPath(threadSession, targetName);

                bool appended = destination.has_value() && fs::exists(receivedPath);
                std::error_code ec;

                fs::path appendSource = receivedPath;
                fs::path normalizedPath = tempDir / "ascii_normalized.tmp";

                if (appended && threadSession.type == TransferType::ASCII)
                {
                    appended = convertCRLFToLFInFile(receivedPath, normalizedPath);
                    appendSource = normalizedPath;
                }

                if (appended)
                {
                    if (fs::exists(*destination))
                    {
                        std::ifstream src(appendSource, std::ios::binary);
                        std::ofstream dst(*destination, std::ios::binary | std::ios::app);
                        dst << src.rdbuf();
                        appended = src.good() || src.eof();
                        appended = appended && dst.good();
                    }
                    else if (threadSession.type == TransferType::ASCII)
                    {
                        fs::rename(normalizedPath, *destination, ec);
                        appended = !ec;
                    }
                    else
                    {
                        fs::rename(receivedPath, *destination, ec);
                        appended = !ec;
                    }
                }

                cleanupSessionTransferDirectory(threadSession);
                threadChannel.close();
                threadSession.dataMode = DataConnMode::NONE;
                updateTransferState(threadSession, false);

                if (!threadSession.abortRequested.load())
                {
                    send_reply(
                        client_socket,
                        appended ? FTPStatus::OK_226 : FTPStatus::ERR_550);
                }
            })
            .detach();

        break;
    }

    case FtpCommand::MDTM:
    {
        auto target = resolveVirtualPath(session, parsed.arg);
        if (!target || !fs::is_regular_file(*target))
        {
            send_reply(client_socket, FTPStatus::ERR_550);
        }
        else
        {
            // Lấy thời gian sửa đổi cuối của file
            std::error_code ec;
            auto ftime = fs::last_write_time(*target, ec);
            if (ec)
            {
                send_reply(client_socket, FTPStatus::ERR_550);
                break;
            }

            auto s_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            std::time_t tt = std::chrono::system_clock::to_time_t(s_time);
            std::tm gmt{};

#ifdef _WIN32
            const bool timeOk = gmtime_s(&gmt, &tt) == 0;
#else
            const bool timeOk = gmtime_r(&tt, &gmt) != nullptr;
#endif

            if (!timeOk)
            {
                send_reply(client_socket, FTPStatus::ERR_550);
                break;
            }

            char timeBuf[30];
            std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d%H%M%S", &gmt);
            send_reply(client_socket, "213 " + std::string(timeBuf) + "\r\n");
        }
        break;
    }

    case FtpCommand::MODE:
    {
        std::string t = parsed.arg;
        for (char &c : t)
        {
            c = static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
        }

        if (t == "S" || t.empty())
        {
            send_reply(client_socket, "200 Mode set to S.\r\n");
        }
        else
        {
            send_reply(client_socket, "504 Command not implemented for that parameter.\r\n");
        }
        break;
    }

    case FtpCommand::ABOR:
    {
        if (!session.transferActive.load() && !dataChannel.isBusy())
        {
            if (dataChannel.isOpened())
            {
                dataChannel.close();
                session.dataMode = DataConnMode::NONE;
            }

            send_reply(client_socket, FTPStatus::OK_225);
            break;
        }

        session.abortRequested.store(true);
        dataChannel.abortTransfer();

        // Wait until the transfer thread has finished its own DataChannel
        // cleanup. transferActive is cleared only at the end of that thread.
        for (int i = 0; i < 160 && session.transferActive.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (session.transferActive.load())
        {
            send_reply(client_socket, FTPStatus::ERR_426);
            break;
        }

        if (dataChannel.isOpened() && !dataChannel.isBusy())
        {
            dataChannel.close();
        }

        session.dataMode = DataConnMode::NONE;

        // RFC 959 active-transfer abort sequence.
        send_reply(client_socket, FTPStatus::ERR_426);
        send_reply(client_socket, "226 ABOR command successful.\r\n");
        break;
    }
    case FtpCommand::HELP:
    {
        send_reply(client_socket, buildHelpReply(parsed.arg));
        break;
    }

    case FtpCommand::HASH:
    {
        if (parsed.arg.empty())
        {
            send_reply(client_socket, FTPStatus::ERR_501);
            break;
        }

        auto target = resolveVirtualPath(session, parsed.arg);
        if (!target || !fs::is_regular_file(*target))
        {
            send_reply(client_socket, FTPStatus::ERR_550);
            break;
        }

        const std::string digest = SHA256::hashFile(target->string());
        if (digest.empty())
        {
            send_reply(client_socket, FTPStatus::ERR_550);
            break;
        }

        send_reply(client_socket, "213 SHA256 " + digest + "\r\n");
        break;
    }
    case FtpCommand::STAT:
    {
        if (parsed.arg.empty())
        {
            // 1. STAT không tham số: Trả về trạng thái Session hiện tại
            std::string dataModeStr = "NONE";
            if (session.dataMode == DataConnMode::PASSIVE)
                dataModeStr = "PASSIVE";
            else if (session.dataMode == DataConnMode::ACTIVE)
                dataModeStr = "ACTIVE";

            std::string statusMsg =
                "211- Hybrid FTP Server Status:\r\n"
                " Connected user: " +
                (session.username.empty() ? "Anonymous" : session.username) + "\r\n"
                                                                              " Current directory: " +
                session.currentDir + "\r\n"
                                     " Transfer Type: " +
                (session.type == TransferType::ASCII ? "ASCII" : "BINARY") + "\r\n"
                                                                             " Data Connection Mode: " +
                dataModeStr + "\r\n"
                              "211 End of status.\r\n";
            send_reply(client_socket, statusMsg);
        }
        else
        {
            // 2. STAT có tham số: Xem thông tin file/thư mục qua Control Socket
            auto target = resolveVirtualPath(session, parsed.arg);
            if (!target || !fs::exists(*target))
            {
                send_reply(client_socket, FTPStatus::ERR_550);
            }
            else if (fs::is_regular_file(*target))
            {
                std::error_code ec;
                auto sz = fs::file_size(*target, ec);
                std::string fileMsg =
                    "213- File status for " + parsed.arg + ":\r\n"
                                                           " Size: " +
                    std::to_string(sz) + " bytes\r\n"
                                         "213 End of status.\r\n";
                send_reply(client_socket, fileMsg);
            }
            else if (fs::is_directory(*target))
            {
                std::string dirMsg = "212- Directory status for " + parsed.arg + ":\r\n";
                for (auto &entry : fs::directory_iterator(*target))
                {
                    dirMsg += (entry.is_directory() ? "d " : "- ") + entry.path().filename().string() + "\r\n";
                }
                dirMsg += "212 End of status.\r\n";
                send_reply(client_socket, dirMsg);
            }
        }
        break;
    }

    default:
    {
        send_reply(client_socket, FTPStatus::ERR_500);
        break;
    }
    }
    break;
    }
}
}

// ---------------------------------------------------------------------
// handle_client_session(): TOÀN BỘ vòng đời phục vụ 1 client, từ lúc
// vừa accept() xong tới lúc đóng kết nối. Hàm này sẽ được chạy trong
// 1 std::thread RIÊNG cho mỗi client -> nhiều client chạy song song.
// Nhận client_socket theo GIÁ TRỊ (không phải &) vì mỗi thread cần
// bản sao độc lập của biến này, sống suốt đời thread đó.
// ---------------------------------------------------------------------
void handle_client_session(
    SOCKET client_socket,
    const std::string &clientIp)
{
    log_info("New client connected from " + clientIp + ".");

    auto session = std::make_shared<ClientSession>();
    auto dataChannel = std::make_shared<DataChannel>();

    session->controlSocketFd = static_cast<int>(client_socket);
    session->sessionId = g_nextSessionId.fetch_add(1);
    session->clientIp = clientIp;

    g_sessionRegistry.addSession(session->sessionId, clientIp);
    g_sessionRegistry.printSessions();

    send_reply(client_socket, FTPStatus::OK_220);

    std::string recvBuffer;
    char buffer[BUFFER_SIZE];
    bool quitRequested = false;

    while (!quitRequested)
    {
        const int bytesRead = recv(
            client_socket,
            buffer,
            BUFFER_SIZE - 1,
            0);

        if (bytesRead <= 0)
        {
            log_info("Client disconnected: " + clientIp + ".");
            break;
        }

        buffer[bytesRead] = '\0';
        recvBuffer.append(buffer, static_cast<std::size_t>(bytesRead));

        std::size_t position = 0;

        while ((position = recvBuffer.find('\n')) != std::string::npos)
        {
            std::string commandLine = recvBuffer.substr(0, position);
            recvBuffer.erase(0, position + 1);

            if (!commandLine.empty() && commandLine.back() == '\r')
            {
                commandLine.pop_back();
            }

            if (commandLine.empty())
            {
                continue;
            }

            const ParsedCommand parsed = parseLine(commandLine);

            log_info(
                "Session " + std::to_string(session->sessionId)
                + " | Client " + clientIp
                + " | COMMAND | " + commandForLog(parsed));

            handle_client_command(
                client_socket,
                session,
                dataChannel,
                commandLine);

            if (parsed.cmd == FtpCommand::QUIT)
            {
                quitRequested = true;
                break;
            }
        }
    }

    // Do not destroy per-session state while a detached transfer thread still
    // owns it. Request cancellation and wait for the cooperative loop to stop.
    if (session->transferActive.load() || dataChannel->isBusy())
    {
        session->abortRequested.store(true);
        dataChannel->abortTransfer();

        for (int i = 0; i < 180 && session->transferActive.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    if (dataChannel->isOpened() && !dataChannel->isBusy())
    {
        dataChannel->close();
    }

    cleanupSessionTransferDirectory(*session);

    g_sessionRegistry.removeSession(session->sessionId);
    g_sessionRegistry.printSessions();

    SocketPlatform::close(client_socket);
    log_info("Closed client connection from " + clientIp + ".");
}

int main()
{
    if (!Logger::initialize("logs/server.log", true))
    {
        std::cerr << "[LOGGER][ERROR] Cannot open logs/server.log" << std::endl;
        return 1;
    }

    if (!init_sockets())
    {
        log_error("Failed to initialize socket platform.");
        return 1;
    }

    if (!g_authenticationManager.loadUsers("config/users.txt"))
    {
        log_error("Cannot load config/users.txt. Server will stop.");
        cleanup_sockets();
        return 1;
    }

    log_info("Starting Hybrid FTP Server on Port " + std::to_string(SERVER_PORT) + "...");

    // Tạo thư mục gốc ảo trên đĩa nếu chưa có -- BẮT BUỘC phải chạy trước
    // khi server bắt đầu nhận client, nếu không MKD/CWD/RETR... sẽ luôn
    // thất bại vì thư mục cha (SERVER_ROOT) không tồn tại.
    std::error_code root_ec;
    fs::create_directories(SERVER_ROOT, root_ec);
    log_info("Server root directory: " + SERVER_ROOT.string());

    // BƯỚC 1: Tạo TCP Socket
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET)
    {
        log_error("Socket creation failed.");
        cleanup_sockets();
        return 1;
    }

    // Thiết lập tùy chọn cho phép dùng lại cổng nhanh (SO_REUSEADDR)
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    // BƯỚC 2: Cấu hình địa chỉ IP và Port
    sockaddr_in address{};
    address.sin_family = AF_INET;          // IPv4
    address.sin_addr.s_addr = INADDR_ANY;  // Chấp nhận mọi IP
    address.sin_port = htons(SERVER_PORT); // Cổng 2121

    // BƯỚC 3: Gắn Socket vào Port (Bind)
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
    {
        log_error("Bind failed. Port might be in use.");
        SocketPlatform::close(server_fd);
        cleanup_sockets();
        return 1;
    }

    // BƯỚC 4: Lắng nghe kết nối (Listen)
    if (listen(server_fd, SOMAXCONN) == SOCKET_ERROR)
    {
        log_error("Listen failed.");
        SocketPlatform::close(server_fd);
        cleanup_sockets();
        return 1;
    }

    log_info("Server is listening on port " + std::to_string(SERVER_PORT) + ". Waiting for connections...");

    // BƯỚC 5: Vòng lặp liên tục chấp nhận Client
    while (true)
    {
        sockaddr_in client_addr{};
        SocketPlatform::Length addrlen = sizeof(client_addr);

        // Chờ kết nối mới từ Client
        SOCKET client_socket = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
        if (client_socket == INVALID_SOCKET)
        {
            log_error("Accept connection failed.");
            continue;
        }

        char clientIpBuffer[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &client_addr.sin_addr, clientIpBuffer, sizeof(clientIpBuffer));
        const std::string clientIp = clientIpBuffer;

        log_info("Accepted TCP client: " + clientIp);

        // One thread handles one independent FTP control session.
        std::thread(handle_client_session, client_socket, clientIp).detach();
    } // Kết thúc vòng lặp accept (Server tiếp tục chờ Client tiếp theo)

    // Đóng Server socket khi dừng Server
    SocketPlatform::close(server_fd);
    cleanup_sockets();
    return 0;
}