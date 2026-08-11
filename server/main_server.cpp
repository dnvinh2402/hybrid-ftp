#include <iostream>
#include <cstring>
#include <string>
#include <filesystem>
#include <optional>
#include <random>
#include <thread>
#include <fstream>
#include "../common/logger.h"
#include "../common/protocol.h"
#include "../network/data_channel.h"
#include "../network/data_channel_config.h"

// Khai báo thư viện Socket tương thích cả Windows và Linux
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

// ---------------------------------------------------------------------
// resolveVirtualPath(): chuyển 1 đường dẫn "ảo" (client nhìn thấy, dạng
// "/", "/photos", "../secret") thành đường dẫn THẬT trên đĩa, đồng thời
// kiểm tra nó không vượt ra ngoài SERVER_ROOT.
//
// Trả về std::nullopt nếu đường dẫn không hợp lệ / cố gắng thoát ra ngoài.
// ---------------------------------------------------------------------

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

// Chuyển 1 đường dẫn THẬT trên đĩa (đã nằm trong SERVER_ROOT) ngược lại
// thành đường dẫn "ảo" để hiển thị / lưu vào session.currentDir.
std::string toVirtualPath(const fs::path &realPath)
{
    fs::path rel = fs::relative(realPath, SERVER_ROOT); // tạo đường dẫn tương đối từ SERVER_ROOT đến realPat
    if (rel == ".")
        return "/"; // dang o dung thu muc goc
    return "/" + rel.generic_string();
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

// Hàm xử lý phân tích và phản hồi lệnh FTP
void handle_client_command(SOCKET client_socket, ClientSession &session, DataChannel &dataChannel, const std::string &raw_line)
{
    log_debug("Received command: " + raw_line);
    ParsedCommand parsed = parseLine(raw_line);

    // Các lệnh cần đăng nhập trước mới được dùng (mọi lệnh trừ USER/PASS/QUIT/NOOP)
    static const bool needsAuth[] = {}; // (không dùng - kiểm tra thủ công bên dưới cho rõ ràng)

    switch (parsed.cmd)
    {
    // ---------------- Nhóm đăng nhập (không đổi) ----------------
    case FtpCommand::USER:
    {
        session.username = parsed.arg;
        log_info("Client set username: " + session.username);
        send_reply(client_socket, FTPStatus::NEED_PASS_331);
        break;
    }
    case FtpCommand::PASS:
    {
        if (session.username.empty())
        {
            send_reply(client_socket, FTPStatus::ERR_530);
        }
        else
        {
            session.authenticated = true;
            log_info("User '" + session.username + "' authenticated.");
            send_reply(client_socket, FTPStatus::OK_230);
        }
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
            auto target = resolveVirtualPath(session, "..");
            session.currentDir = toVirtualPath(*target); // ".." luôn hợp lệ, root tự chặn tại "/"
            send_reply(client_socket, FTPStatus::OK_250);
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

        // -------- Kênh dữ liệu: thoả thuận địa chỉ/port --------
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
                dataChannel.close();

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

            // receiveFile() TỰ BLOCK chờ đúng theo Stop-and-Wait, tới khi
            // nhận đủ FIN hoặc vượt quá số lần timeout liên tiếp cho phép.
            bool ok = dataChannel.receiveFile();
            if (!ok)
            {
                send_reply(client_socket, FTPStatus::ERR_426);
                break;
            }

            // FileReceiver LUÔN ghi ra "server_files/<tên client tự gửi trong
            // metadata>" (thiết kế gốc của đồng đội, không phụ thuộc thư mục
            // ảo hiện tại của session) -- phải MỜI file đó vào đúng vị trí
            // trong SERVER_ROOT theo tên mà lệnh STOR yêu cầu (parsed.arg).
            const TransferSession &recvInfo = dataChannel.getReceiveTransferSession();
            fs::path receivedPath = fs::path("server_files") / recvInfo.fileName;

            auto dest = resolveVirtualPath(session, parsed.arg);
            if (!dest || !fs::exists(receivedPath))
            {
                log_error("STOR: received file not found at expected temp location.");
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

            send_reply(client_socket, FTPStatus::OK_226);
            break;
        }

        case FtpCommand::RETR:
        {
            // if (session.dataMode == DataConnMode::NONE || !dataChannel.isOpened())
            // {
            //     send_reply(client_socket, FTPStatus::ERR_425);
            //     break;
            // }
            // if (session.dataMode == DataConnMode::PASSIVE)
            // {
            //     // Xem ghi chú thiết kế: PASV chỉ cho server 1 port để NHẬN,
            //     // chưa đủ thông tin để biết gửi RETR tới đâu bên client.
            //     // Cần thêm bước bắt tay trước khi hỗ trợ tổ hợp này.
            //     log_error("RETR via PASV not yet supported -- use PORT (active mode) instead.");
            //     send_reply(client_socket, FTPStatus::ERR_425);
            //     break;
            // }

            // auto target = resolveVirtualPath(session, parsed.arg);
            // if (!target || !fs::is_regular_file(*target))
            // {
            //     send_reply(client_socket, FTPStatus::ERR_550);
            //     break;
            // }

            // send_reply(client_socket, FTPStatus::OK_150);
            // bool ok = dataChannel.sendFile(target->string(), session.dataIp,
            //                                static_cast<unsigned short>(session.dataPort));
            // send_reply(client_socket, ok ? FTPStatus::OK_226 : FTPStatus::ERR_426);
            // break;

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

            bool ok = dataChannel.sendFile(target->string(), destIp, destPort);
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
                for (auto &entry : fs::directory_iterator(*dirPath))
                {
                    listFile << (entry.is_directory() ? "d " : "- ")
                             << entry.path().filename().string() << "\n";
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

            bool ok = dataChannel.sendFile(tempListing.string(), destIp, destPort);
            std::error_code ec;
            fs::remove(tempListing, ec);
            send_reply(client_socket, ok ? FTPStatus::OK_226 : FTPStatus::ERR_426);
            break;
        }

        // -------- Chưa cài: chưa yêu cầu trong bước này --------
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

            bool ok = dataChannel.sendFile(tempListing.string(), destIp, destPort);
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

            // STOU: theo RFC 959, server TỰ SINH tên file duy nhất, KHÔNG
            // dùng nguyên tên client gợi ý -- chỉ giữ lại phần đuôi mở
            // rộng (nếu có) để file vẫn nhận diện được loại nội dung.
            std::string ext = parsed.arg.empty() ? "" : fs::path(parsed.arg).extension().string();
            auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
            std::string uniqueName = "upload_" + std::to_string(session.controlSocketFd) +
                                     "_" + std::to_string(nowMs) + ext;

            send_reply(client_socket, FTPStatus::OK_150);
            bool ok = dataChannel.receiveFile();
            if (!ok)
            {
                send_reply(client_socket, FTPStatus::ERR_426);
                break;
            }

            const TransferSession &recvInfo = dataChannel.getReceiveTransferSession();
            fs::path receivedPath = fs::path("server_files") / recvInfo.fileName;
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
            // dòng này để biết tên file trên server (vì không phải tên
            // nó tự đặt).
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
            bool ok = dataChannel.receiveFile();
            if (!ok)
            {
                send_reply(client_socket, FTPStatus::ERR_426);
                break;
            }

            const TransferSession &recvInfo = dataChannel.getReceiveTransferSession();
            fs::path receivedPath = fs::path("server_files") / recvInfo.fileName;
            auto dest = resolveVirtualPath(session, parsed.arg);
            if (!dest || !fs::exists(receivedPath))
            {
                send_reply(client_socket, FTPStatus::ERR_550);
                break;
            }

            if (fs::exists(*dest))
            {
                // File đích đã tồn tại -> NỐI dữ liệu vừa nhận vào CUỐI
                // file đó, thay vì ghi đè (đúng ngữ nghĩa APPE khác STOR).
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
            send_reply(client_socket, "225 ABOR command successful.\r\n");
            break;
        }

        case FtpCommand::HELP:
        {
            std::string helpText = 
                "214-The following commands are recognized:\r\n"
                " USER PASS PWD CWD CDUP MKD RMD DELE RNFR RNTO\r\n"
                " TYPE SIZE PORT PASV STOR RETR LIST NLST STOU APPE\r\n"
                " MDTM MODE ABOR HELP HASH NOOP QUIT\r\n"
                "214 Help OK.\r\n";
            send_reply(client_socket, helpText);
            break;
        }

        case FtpCommand::HASH:
        {
            send_reply(client_socket, "200 HASH command accepted.\r\n");
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
    (void)needsAuth; // dập cảnh báo của compiler khi biến chưa được sử dụng.
}

// ---------------------------------------------------------------------
// handle_client_session(): TOÀN BỘ vòng đời phục vụ 1 client, từ lúc
// vừa accept() xong tới lúc đóng kết nối. Hàm này sẽ được chạy trong
// 1 std::thread RIÊNG cho mỗi client -> nhiều client chạy song song.
// Nhận client_socket theo GIÁ TRỊ (không phải &) vì mỗi thread cần
// bản sao độc lập của biến này, sống suốt đời thread đó.
// ---------------------------------------------------------------------
void handle_client_session(SOCKET client_socket)
{
    log_info("New client connected!");

    // ClientSession khai báo LOCAL trong hàm chạy trên thread riêng
    // -> mỗi thread có 1 session hoàn toàn độc lập, không đụng chạm
    // tới session của thread khác => không cần mutex ở đây.
    ClientSession session;
    DataChannel dataChannel; // Kênh dữ liệu UDP RIÊNG của client này, mở khi PASV/PORT
    session.controlSocketFd = static_cast<int>(client_socket);

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

    closesocket(client_socket);
    log_info("Closed client connection.");
}

int main()
{
    init_sockets();
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
    if (listen(server_fd, 3) == SOCKET_ERROR)
    {
        log_error("Listen failed.");
        closesocket(server_fd);
        cleanup_sockets();
        return 1;
    }

    log_info("Server is listening on port " + std::to_string(SERVER_PORT) + ". Waiting for connections...");

    // BƯỚC 5: Vòng lặp liên tục chấp nhận Client
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

        // Giao toàn bộ việc phục vụ client này cho 1 thread MỚI.
        // .detach() nghĩa là main() KHÔNG chờ thread này chạy xong —
        // thread tự sống độc lập, tự dọn dẹp khi hàm handle_client_session
        // return (client ngắt kết nối / QUIT). Nhờ vậy vòng lặp while(true)
        // ở dưới quay lại accept() NGAY LẬP TỨC để nhận client tiếp theo.
        std::thread(handle_client_session, client_socket).detach();
    } // Kết thúc vòng lặp accept (Server tiếp tục chờ Client tiếp theo)

    // Đóng Server socket khi dừng Server
    closesocket(server_fd);
    cleanup_sockets();
    return 0;
}