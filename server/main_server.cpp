#include <iostream>
#include <cstring>
#include <string>
#include <filesystem>
#include <optional>
#include <random>
#include <thread>
#include <fstream>
#include <unordered_map>
#include <atomic>
#include "authentication_manager.h"
#include "session_registry.h"
#include "../common/sha256.h"
#include "../common/logger.h"
#include "../common/protocol.h"
#include "../network/data_channel.h"
#include "../network/data_channel_config.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#define SERVER_PORT 2121
#define BUFFER_SIZE 1024

namespace fs = std::filesystem;
// chứa đường dẫn tuyệt đối đến thư mục ftp_root
const fs::path SERVER_ROOT = fs::absolute("ftp_root");

// New feature modules. They are shared by all client threads.
AuthenticationManager g_authenticationManager;
SessionRegistry g_sessionRegistry;

// Hàm khởi tạo thư viện Socket (bắt buộc trên Windows)
void init_sockets()
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        log_error("Failed to initialize Winsock.");
        exit(EXIT_FAILURE);
    }
#endif
}

// Hàm dọn dẹp Socket khi đóng chương trình
void cleanup_sockets()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

void send_reply(SOCKET client_socket, const std::string &reply)
{
    send(client_socket, reply.c_str(), reply.length(), 0);
    log_debug("Sent reply: " + reply);
}


std::optional<fs::path> resolveVirtualPath(const ClientSession &session, const std::string &arg)
{
    // Nếu arg rỗng -> dùng thư mục hiện tại của session
    // Nếu arg bắt đầu bằng / → coi như đường dẫn tuyệt đối trong không gian ảo của FTP
    fs::path virtualPath = arg.empty() ? fs::path(session.currentDir)
                                       : (arg[0] == '/' ? fs::path(arg)
                                                        : fs::path(session.currentDir) / arg); // đường dẫn “ảo” mà client muốn truy cập.

    // Ghép với SERVER_ROOT rồi rút gọn ".." "." (lexically_normal không cần
    // file tồn tại thật, an toàn để kiểm tra trước khi đụng vào đĩa)
    fs::path combined = (SERVER_ROOT / virtualPath.relative_path()).lexically_normal(); // Ghép virtualPath với SERVER_ROOT để tạo đường dẫn thực trên máy.

    // Chuyển cả 2 về dạng chuỗi để so sánh "combined có nằm bên trong SERVER_ROOT không"
    std::string rootStr = SERVER_ROOT.string();
    std::string combinedStr = combined.string();
    if (combinedStr.size() < rootStr.size() ||
        combinedStr.compare(0, rootStr.size(), rootStr) != 0)
    {
        return std::nullopt; // cố thoát ra ngoài -> từ chối
    }
    return combined;
}

// Hàm tự động sinh tên file không bị trùng
fs::path getUniquePath(const fs::path& targetPath)
{
    if (!fs::exists(targetPath))
    {
        return targetPath;
    }

    fs::path parent = targetPath.parent_path();
    std::string stem = targetPath.stem().string();       // Tên file không chứa đuôi (vd: "picture")
    std::string ext = targetPath.extension().string();   // Đuôi file (vd: ".png")

    int counter = 1;
    fs::path newPath;
    do
    {
        newPath = parent / (stem + " (" + std::to_string(counter) + ")" + ext);
        counter++;
    } while (fs::exists(newPath));

    return newPath;
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
        (permissions & fs::perms::owner_read)
                != fs::perms::none
            ? 'r'
            : '-';

    result +=
        (permissions & fs::perms::owner_write)
                != fs::perms::none
            ? 'w'
            : '-';

    result +=
        (permissions & fs::perms::owner_exec)
                != fs::perms::none
            ? 'x'
            : '-';

    result +=
        (permissions & fs::perms::group_read)
                != fs::perms::none
            ? 'r'
            : '-';

    result +=
        (permissions & fs::perms::group_write)
                != fs::perms::none
            ? 'w'
            : '-';

    result +=
        (permissions & fs::perms::group_exec)
                != fs::perms::none
            ? 'x'
            : '-';

    result +=
        (permissions & fs::perms::others_read)
                != fs::perms::none
            ? 'r'
            : '-';

    result +=
        (permissions & fs::perms::others_write)
                != fs::perms::none
            ? 'w'
            : '-';

    result +=
        (permissions & fs::perms::others_exec)
                != fs::perms::none
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

    return std::string(1, type)
           + " "
           + getPermissionString(
                 entry.path())
           + " "
           + sizeText
           + " "
           + entry.path()
                 .filename()
                 .string();
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
    socklen_t len = sizeof(localAddr);
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

std::string buildHelpReply(const std::string& commandArgument)
{
    if (commandArgument.empty())
    {
        return
            "214-The following commands are recognized:\r\n"
            " USER PASS QUIT NOOP PWD CWD CDUP MKD RMD\r\n"
            " LIST NLST STAT SIZE MDTM TYPE MODE PORT PASV\r\n"
            " RETR STOR STOU APPE DELE RNFR RNTO HASH ABOR HELP\r\n"
            "214 Help OK.\r\n";
    }

    std::string command = commandArgument;
    for (char& character : command)
    {
        character = static_cast<char>(std::toupper(character));
    }

    static const std::unordered_map<std::string, std::string> helpTable = {
        {"USER", "USER <username> - Send username for authentication."},
        {"PASS", "PASS <password> - Send password to complete login."},
        {"QUIT", "QUIT - Close the FTP session."},
        {"NOOP", "NOOP - Keep the control connection alive."},
        {"PWD",  "PWD - Show the current server directory."},
        {"CWD",  "CWD <path> - Change the current directory."},
        {"CDUP", "CDUP - Move to the parent directory."},
        {"MKD",  "MKD <dirname> - Create a directory."},
        {"RMD",  "RMD <dirname> - Remove an empty directory."},
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
        {"HELP", "HELP [command] - Show command usage."}
    };

    const auto iterator = helpTable.find(command);
    if (iterator == helpTable.end())
    {
        return "501 Unknown HELP command.\r\n";
    }

    return "214 " + iterator->second + "\r\n";
}
// Chuyển dòng sang CRLF (\r\n) khi gửi ở chế độ ASCII
std::string convertToCRLF(const std::string& buffer) {
    std::string result;
    result.reserve(buffer.size() * 1.1); 
    for (size_t i = 0; i < buffer.size(); ++i) {
        if (buffer[i] == '\n' && (i == 0 || buffer[i - 1] != '\r')) {
            result += "\r\n";
        } else {
            result += buffer[i];
        }
    }
    return result;
}

// chuyênr đổi dòng CRLF (\r\n) sang LF (\n) khi nhận ở chế độ ASCII
std::string convertFromCRLF(const std::string& buffer) {
    std::string result;
    result.reserve(buffer.size());
    for (size_t i = 0; i < buffer.size(); ++i) {
        if (buffer[i] == '\r' && i + 1 < buffer.size() && buffer[i + 1] == '\n') {
            result += '\n';
            ++i; 
        } else {
            result += buffer[i];
        }
    }
    return result;
}
void updateTransferState(ClientSession& session, bool transferActive)
{
    session.transferActive.store(transferActive);

    if (transferActive)
    {
        session.abortRequested.store(false);
    }
    g_sessionRegistry.setTransferActive(session.sessionId, transferActive);
    g_sessionRegistry.printSessions();
}

static bool convertCRLFToLFInFile(const fs::path& srcPath, const fs::path& dstPath) {
    std::ifstream in(srcPath, std::ios::binary);
    std::ofstream out(dstPath, std::ios::binary);
    if (!in.is_open() || !out.is_open()) return false;

    char c;
    while (in.get(c)) {
        if (c == '\r') {
            if (in.peek() == '\n') {
                in.get(c); // Bỏ qua '\r', lấy ký tự '\n' tiếp theo để ghi
            }
        }
        out.put(c);
    }
    return true;
}

// Hàm xử lý phân tích và phản hồi lệnh FTP
void handle_client_command(SOCKET client_socket, ClientSession &session, DataChannel &dataChannel, const std::string &raw_line)
{
    log_debug("Received command: " + raw_line);
    ParsedCommand parsed = parseLine(raw_line);


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
            for (auto &c : t)
                c = toupper(c);
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
            static std::mt19937 rng(std::random_device{}());
            static std::uniform_int_distribution<int> distPort(50000, 51000);
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

    send_reply(client_socket, FTPStatus::OK_150);
    updateTransferState(session, true);

    // 1. Nhận file vào thư mục staging độc lập của session
    std::string stagingDir = "server_files/session_" + std::to_string(session.sessionId);
    bool ok = dataChannel.receiveFile(stagingDir);
    updateTransferState(session, false);

    if (!ok)
    {
        send_reply(client_socket, FTPStatus::ERR_426);
        std::error_code cleanupEc;
        fs::remove_all(stagingDir, cleanupEc);
        break;
    }

    // 2. Xác định vị trí file tạm và vị trí đích
    const TransferSession &recvInfo = dataChannel.getReceiveTransferSession();
    fs::path receivedPath = fs::path(stagingDir) / recvInfo.fileName;

    auto dest = resolveVirtualPath(session, parsed.arg);
    if (!dest || !fs::exists(receivedPath))
    {
        log_error("STOR: received file not found at expected temp location.");
        send_reply(client_socket, FTPStatus::ERR_550);
        
        std::error_code cleanupEc;
        fs::remove_all(stagingDir, cleanupEc);
        break;
    }

    // Tự động đổi tên chống ghi đè
    fs::path finalDest = getUniquePath(*dest);

    std::error_code ec;

    // 3. Di chuyển file từ Staging ra thư mục thực tế theo finalDest
    if (session.type == TransferType::ASCII)
    {
        // ASCII mode: Chuyển CRLF -> LF và lưu thẳng vào finalDest
        if (!convertCRLFToLFInFile(receivedPath, finalDest))
        {
            log_error("STOR: Failed to convert ASCII CRLF to LF");
            send_reply(client_socket, FTPStatus::ERR_550);
            
            std::error_code cleanupEc;
            fs::remove_all(stagingDir, cleanupEc);
            break;
        }
    }
    else
    {
        // BINARY mode: Đổi tên / di chuyển file vào finalDest
        fs::rename(receivedPath, finalDest, ec);
        
        // Fallback: Nếu rename bị lỗi do khác phân vùng đĩa hoặc OS lock
        if (ec)
        {
            ec.clear();
            fs::copy_file(receivedPath, finalDest, fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                log_error("STOR: Failed to move binary file to destination: " + ec.message());
                send_reply(client_socket, FTPStatus::ERR_550);
                
                std::error_code cleanupEc;
                fs::remove_all(stagingDir, cleanupEc);
                break;
            }
        }
    }

    // 4. Dọn dẹp sạch toàn bộ thư mục Staging của Session
    std::error_code cleanupEc;
    fs::remove_all(stagingDir, cleanupEc);

    send_reply(client_socket, FTPStatus::OK_226);
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

            // Hỗ trợ PASV cho RETR bằng cách chờ gói Handshake từ Client
            if (session.dataMode == DataConnMode::PASSIVE)
            {
                send_reply(client_socket, FTPStatus::OK_150);
                std::string learnedIp;
                unsigned short learnedPort;
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

            updateTransferState(session, true);
            bool ok = dataChannel.sendFile(target->string(), destIp, destPort, session.type);
            updateTransferState(session, false);
            send_reply(client_socket, ok ? FTPStatus::OK_226 : FTPStatus::ERR_426);
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

            std::string ext = parsed.arg.empty() ? "" : fs::path(parsed.arg).extension().string();
            auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
            std::string uniqueName = "upload_" + std::to_string(session.controlSocketFd) +
                                     "_" + std::to_string(nowMs) + ext;

            send_reply(client_socket, FTPStatus::OK_150);
            updateTransferState(session, true);
            std::string stagingDir = "server_files/session_" + std::to_string(session.sessionId);
            bool ok = dataChannel.receiveFile(stagingDir);
            updateTransferState(session, false);
            if (!ok)
            {
                send_reply(client_socket, FTPStatus::ERR_426);
                break;
            }

            const TransferSession &recvInfo = dataChannel.getReceiveTransferSession();
            fs::path receivedPath = fs::path(stagingDir) / recvInfo.fileName;
            auto dest = resolveVirtualPath(session, uniqueName);
            if (!dest || !fs::exists(receivedPath))
            {
                send_reply(client_socket, FTPStatus::ERR_550);
                break;
            }

            std::error_code ec;
            fs::rename(receivedPath, *dest, ec);
            if (ec)
            {
                send_reply(client_socket, FTPStatus::ERR_550);
                break;
            }

            // Reply 226 kèm tên thật đã lưu -- client BẮT BUỘC phải đọc
            std::error_code cleanupEc;
            fs::remove(stagingDir, cleanupEc);
            send_reply(client_socket, "226 Transfer complete. Stored as \"" + uniqueName + "\".\r\n");
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

            send_reply(client_socket, FTPStatus::OK_150);
            updateTransferState(session, true);
            std::string stagingDir = "server_files/session_" + std::to_string(session.sessionId);
            bool ok = dataChannel.receiveFile(stagingDir);
            updateTransferState(session, false);
            if (!ok)
            {
                send_reply(client_socket, FTPStatus::ERR_426);
                break;
            }

            const TransferSession &recvInfo = dataChannel.getReceiveTransferSession();
            fs::path receivedPath = fs::path(stagingDir) / recvInfo.fileName;
            auto dest = resolveVirtualPath(session, parsed.arg);
            if (!dest || !fs::exists(receivedPath))
            {
                send_reply(client_socket, FTPStatus::ERR_550);
                break;
            }

            if (fs::exists(*dest))
            {
                std::ifstream src(receivedPath, std::ios::binary);
                std::ofstream dst(*dest, std::ios::binary | std::ios::app);
                dst << src.rdbuf();
                src.close();
                dst.close();
                std::error_code ec2;
                fs::remove(receivedPath, ec2);
            }
            else
            {
                // File đích chưa tồn tại -> hành vi giống STOR bình thường.
                std::error_code ec;
                fs::rename(receivedPath, *dest, ec);
                if (ec)
                {
                    send_reply(client_socket, FTPStatus::ERR_550);
                    break;
                }
                
            }

            std::error_code cleanupEc;
            fs::remove(stagingDir, cleanupEc);

            send_reply(client_socket, FTPStatus::OK_226);
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
                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                std::time_t tt = std::chrono::system_clock::to_time_t(s_time);
                std::tm* gmt = std::gmtime(&tt);

                char timeBuf[30];
                std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d%H%M%S", gmt);
                send_reply(client_socket, "213 " + std::string(timeBuf) + "\r\n");
            }
            break;
        }

        case FtpCommand::MODE:
        {
            std::string t = parsed.arg;
            for (auto &c : t) c = toupper(c);

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
            // TCP/control-plane half of ABOR.
            // The Data Plane owner must make FileSender/FileReceiver observe
            // session.abortRequested while a transfer is running.
            if (!session.transferActive.load())
            {
                send_reply(client_socket, FTPStatus::OK_225);
                break;
            }

            //dataChannel.requestAbort();

            // 426: the data transfer is being aborted.
            // 226: the ABOR control command itself was accepted.
            send_reply(client_socket, FTPStatus::ERR_426);
            send_reply(client_socket, "226 Abort request accepted.\r\n");
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
                if (session.dataMode == DataConnMode::PASSIVE) dataModeStr = "PASSIVE";
                else if (session.dataMode == DataConnMode::ACTIVE) dataModeStr = "ACTIVE";

                std::string statusMsg = 
                    "211- Hybrid FTP Server Status:\r\n"
                    " Connected user: " + (session.username.empty() ? "Anonymous" : session.username) + "\r\n"
                    " Current directory: " + session.currentDir + "\r\n"
                    " Transfer Type: " + (session.type == TransferType::ASCII ? "ASCII" : "BINARY") + "\r\n"
                    " Data Connection Mode: " + dataModeStr + "\r\n"
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
                        " Size: " + std::to_string(sz) + " bytes\r\n"
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
void handle_client_session(SOCKET client_socket, const std::string& clientIp, int sessionId)
{
    log_info("New client connected from " + clientIp + " [Session ID: " + std::to_string(sessionId) + "]!");

    // ClientSession khai báo LOCAL trong hàm chạy trên thread riêng
    // -> mỗi thread có 1 session hoàn toàn độc lập, không đụng chạm
    // tới session của thread khác => không cần mutex ở đây.
    ClientSession session;
    DataChannel dataChannel; // Kênh dữ liệu UDP RIÊNG của client này, mở khi PASV/PORT
    session.controlSocketFd = static_cast<int>(client_socket);
    session.sessionId = sessionId;
    session.clientIp = clientIp;

    g_sessionRegistry.addSession(session.sessionId, clientIp);
    g_sessionRegistry.printSessions();

    send_reply(client_socket, FTPStatus::OK_220);

    std::string recv_buffer = "";
    char buffer[BUFFER_SIZE];
    bool is_quit = false;

    while (!is_quit)
    {
        int bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0)
        {
            log_info("Client disconnected.");
            break;
        }

        buffer[bytes_read] = '\0';
        recv_buffer.append(buffer, bytes_read);

        size_t pos;
        while ((pos = recv_buffer.find('\n')) != std::string::npos)
        {
            std::string command_line = recv_buffer.substr(0, pos);
            recv_buffer.erase(0, pos + 1);
            if (!command_line.empty() && command_line.back() == '\r')
                command_line.pop_back();

            if (!command_line.empty())
            {
                handle_client_command(client_socket, session, dataChannel, command_line);
                if (command_line.rfind("QUIT", 0) == 0)
                {
                    is_quit = true;
                    break;
                }
            }
        }
    }

    dataChannel.close();

    // Dọn dẹp thư mục tạm staging nếu còn dư sau khi session kết thúc
    std::string stagingDir = "server_files/session_" + std::to_string(session.sessionId);
    std::error_code ec;
    if (fs::exists(stagingDir, ec)) {
        fs::remove_all(stagingDir, ec);
        log_info("Cleaned up staging directory: " + stagingDir);
    }

    g_sessionRegistry.removeSession(session.sessionId);
    g_sessionRegistry.printSessions();

    closesocket(client_socket);
    log_info("Closed client connection from " + clientIp + ".");
}

int main()
{
    init_sockets();

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
        closesocket(server_fd);
        cleanup_sockets();
        return 1;
    }

    // BƯỚC 4: Lắng nghe kết nối (Listen)
    if (listen(server_fd, SOMAXCONN) == SOCKET_ERROR)
    {
        log_error("Listen failed.");
        closesocket(server_fd);
        cleanup_sockets();
        return 1;
    }

    log_info("Server is listening on port " + std::to_string(SERVER_PORT) + ". Waiting for connections...");

    // BƯỚC 5: Vòng lặp liên tục chấp nhận Client
    static std::atomic<int> g_nextSessionId{1};
    while (true)
    {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);

        // Chờ kết nối mới từ Client
        SOCKET client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (client_socket == INVALID_SOCKET)
        {
            log_error("Accept connection failed.");  
            continue;
        }

        char clientIpBuffer[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &client_addr.sin_addr, clientIpBuffer, sizeof(clientIpBuffer));
        const std::string clientIp = clientIpBuffer;

        int currentSessionId = g_nextSessionId++;

        log_info("Accepted TCP client: " + clientIp + " [Session ID: " + std::to_string(currentSessionId) + "]");


        // One thread handles one independent FTP control session.
        std::thread(handle_client_session, client_socket, clientIp, currentSessionId).detach();
    } // Kết thúc vòng lặp accept (Server tiếp tục chờ Client tiếp theo)

    // Đóng Server socket khi dừng Server
    closesocket(server_fd);
    cleanup_sockets();
    return 0;
}