#include <iostream>
#include <cstring>
#include <string>
#include <filesystem>
#include <optional>
#include <random>
#include <thread>                                               
#include "../common/logger.h"
#include "../common/protocol.h"

// Khai báo thư viện Socket tương thích cả Windows và Linux
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#define SERVER_PORT 2121
#define BUFFER_SIZE 1024

namespace fs = std::filesystem;
//chứa đường dẫn tuyệt đối đến thư mục ftp_root
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

std::optional<fs::path> resolveVirtualPath(const ClientSession& session, const std::string& arg) {
    // Nếu arg rỗng -> dùng thư mục hiện tại của session
    //Nếu arg bắt đầu bằng / → coi như đường dẫn tuyệt đối trong không gian ảo của FTP
    fs::path virtualPath = arg.empty() ? fs::path(session.currentDir)
                                        : (arg[0] == '/' ? fs::path(arg)
                                                          : fs::path(session.currentDir) / arg); //đường dẫn “ảo” mà client muốn truy cập.
 
    // Ghép với SERVER_ROOT rồi rút gọn ".." "." (lexically_normal không cần
    // file tồn tại thật, an toàn để kiểm tra trước khi đụng vào đĩa)
    fs::path combined = (SERVER_ROOT / virtualPath.relative_path()).lexically_normal(); //Ghép virtualPath với SERVER_ROOT để tạo đường dẫn thực trên máy.
 
    // Chuyển cả 2 về dạng chuỗi để so sánh "combined có nằm bên trong SERVER_ROOT không"
    std::string rootStr = SERVER_ROOT.string();
    std::string combinedStr = combined.string();
    if (combinedStr.size() < rootStr.size() ||
        combinedStr.compare(0, rootStr.size(), rootStr) != 0) {
        return std::nullopt; // cố thoát ra ngoài -> từ chối
    }
    return combined;
}

// Chuyển 1 đường dẫn THẬT trên đĩa (đã nằm trong SERVER_ROOT) ngược lại
// thành đường dẫn "ảo" để hiển thị / lưu vào session.currentDir.
std::string toVirtualPath(const fs::path& realPath) {
    fs::path rel = fs::relative(realPath, SERVER_ROOT); // tạo đường dẫn tương đối từ SERVER_ROOT đến realPat
    if (rel == ".") return "/"; // dang o dung thu muc goc
    return "/" + rel.generic_string();
}


// Helper cho PORT / PASV: đọc "h1,h2,h3,h4,p1,p2" -> IP + port
// Đây là định dạng chuẩn FTP: p_port = p1*256 + p2
bool parsePortArg(const std::string& arg, std::string& outIp, int& outPort) {
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
std::string getLocalIp(SOCKET client_socket) {
    sockaddr_in localAddr{};
    socklen_t len = sizeof(localAddr);
    getsockname(client_socket, (struct sockaddr*)&localAddr, &len); // lấy địa chỉ IP và port mà socket client_socket đang sử dụng ở phía server.
    char ipBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &localAddr.sin_addr, ipBuf, sizeof(ipBuf)); //chuyển địa chỉ IP nhị phân (localAddr.sin_addr) thành chuỗi dạng "x.x.x.x".
    return std::string(ipBuf);
}


// Dựng chuỗi reply 227 đúng định dạng FTP: "h1,h2,h3,h4,p1,p2"
std::string formatPasvReply(const std::string& ip, int port) {
    int h1, h2, h3, h4;
    sscanf(ip.c_str(), "%d.%d.%d.%d", &h1, &h2, &h3, &h4);
    int p1 = port / 256; // byte cao
    int p2 = port % 256; // byte thấp
    return "227 Entering Passive Mode (" + std::to_string(h1) + "," + std::to_string(h2) + "," +
           std::to_string(h3) + "," + std::to_string(h4) + "," + std::to_string(p1) + "," +
           std::to_string(p2) + ").\r\n";
}

// Hàm xử lý phân tích và phản hồi lệnh FTP
void handle_client_command(SOCKET client_socket, ClientSession& session, const std::string& raw_line) {
    log_debug("Received command: " + raw_line);
    ParsedCommand parsed = parseLine(raw_line);
 
    // Các lệnh cần đăng nhập trước mới được dùng (mọi lệnh trừ USER/PASS/QUIT/NOOP)
    static const bool needsAuth[] = {}; // (không dùng - kiểm tra thủ công bên dưới cho rõ ràng)
 
    switch (parsed.cmd) {
        // ---------------- Nhóm đăng nhập (không đổi) ----------------
        case FtpCommand::USER: {
            session.username = parsed.arg;
            log_info("Client set username: " + session.username);
            send_reply(client_socket, FTPStatus::NEED_PASS_331);
            break;
        }
        case FtpCommand::PASS: {
            if (session.username.empty()) {
                send_reply(client_socket, FTPStatus::ERR_530);
            } else {
                session.authenticated = true;
                log_info("User '" + session.username + "' authenticated.");
                send_reply(client_socket, FTPStatus::OK_230);
            }
            break;
        }
        case FtpCommand::NOOP: {
            send_reply(client_socket, FTPStatus::OK_200);
            break;
        }
        case FtpCommand::QUIT: {
            send_reply(client_socket, FTPStatus::OK_221);
            break;
        }
 
        // ---------------- Từ đây trở xuống: bắt buộc phải login ----------------
        default: {
            if (!session.authenticated) {
                send_reply(client_socket, FTPStatus::ERR_530);
                break;
            }
 
            switch (parsed.cmd) {
                case FtpCommand::PWD: {
                    send_reply(client_socket, "257 \"" + session.currentDir + "\" is current directory.\r\n");
                    break;
                }
 
                case FtpCommand::CWD: {
                    auto target = resolveVirtualPath(session, parsed.arg);
                    if (!target || !fs::is_directory(*target)) {
                        send_reply(client_socket, FTPStatus::ERR_550);
                    } else {
                        session.currentDir = toVirtualPath(*target);
                        send_reply(client_socket, FTPStatus::OK_250);
                    }
                    break;
                }
 
                case FtpCommand::CDUP: {
                    auto target = resolveVirtualPath(session, "..");
                    session.currentDir = toVirtualPath(*target); // ".." luôn hợp lệ, root tự chặn tại "/"
                    send_reply(client_socket, FTPStatus::OK_250);
                    break;
                }
 
                case FtpCommand::MKD: {
                    auto target = resolveVirtualPath(session, parsed.arg);
                    if (!target || parsed.arg.empty()) {
                        send_reply(client_socket, FTPStatus::ERR_501);
                    } else if (fs::exists(*target)) {
                        send_reply(client_socket, FTPStatus::ERR_550);
                    } else {
                        std::error_code ec;
                        fs::create_directory(*target, ec);
                        if (ec) send_reply(client_socket, FTPStatus::ERR_550);
                        else send_reply(client_socket, "257 \"" + toVirtualPath(*target) + "\" directory created.\r\n");
                    }
                    break;
                }
 
                case FtpCommand::RMD: {
                    auto target = resolveVirtualPath(session, parsed.arg);
                    if (!target || !fs::is_directory(*target)) {
                        send_reply(client_socket, FTPStatus::ERR_550);
                    } else {
                        std::error_code ec;
                        fs::remove(*target, ec);
                        if (ec) send_reply(client_socket, FTPStatus::ERR_550);
                        else send_reply(client_socket, FTPStatus::OK_250);
                    }
                    break;
                }
 
                case FtpCommand::DELE: {
                    auto target = resolveVirtualPath(session, parsed.arg);
                    if (!target || !fs::is_regular_file(*target)) {
                        send_reply(client_socket, FTPStatus::ERR_550);
                    } else {
                        std::error_code ec;
                        fs::remove(*target, ec);
                        if (ec) send_reply(client_socket, FTPStatus::ERR_550);
                        else send_reply(client_socket, FTPStatus::OK_250);
                    }
                    break;
                }
 
                case FtpCommand::RNFR: {
                    auto target = resolveVirtualPath(session, parsed.arg);
                    if (!target || !fs::exists(*target)) {
                        send_reply(client_socket, FTPStatus::ERR_550);
                    } else {
                        session.renameFrom = parsed.arg;
                        send_reply(client_socket, FTPStatus::PENDING_350);
                    }
                    break;
                }
 
                case FtpCommand::RNTO: {
                    if (session.renameFrom.empty()) {
                        send_reply(client_socket, FTPStatus::ERR_501); // chưa gọi RNFR trước
                        break;
                    }
                    auto from = resolveVirtualPath(session, session.renameFrom);
                    auto to   = resolveVirtualPath(session, parsed.arg);
                    session.renameFrom.clear(); // dùng 1 lần rồi xoá, dù thành công hay không
                    if (!from || !to || !fs::exists(*from)) {
                        send_reply(client_socket, FTPStatus::ERR_550);
                    } else {
                        std::error_code ec;
                        fs::rename(*from, *to, ec);
                        if (ec) send_reply(client_socket, FTPStatus::ERR_550);
                        else send_reply(client_socket, FTPStatus::OK_250);
                    }
                    break;
                }
 
                case FtpCommand::TYPE: {
                    std::string t = parsed.arg;
                    for (auto& c : t) c = toupper(c);
                    if (t == "A") {
                        session.type = TransferType::ASCII;
                        send_reply(client_socket, "200 Type set to A.\r\n");
                    } else if (t == "I") {
                        session.type = TransferType::BINARY;
                        send_reply(client_socket, "200 Type set to I.\r\n");
                    } else {
                        send_reply(client_socket, FTPStatus::ERR_501);
                    }
                    break;
                }
 
                case FtpCommand::SIZE: {
                    auto target = resolveVirtualPath(session, parsed.arg);
                    if (!target || !fs::is_regular_file(*target)) {
                        send_reply(client_socket, FTPStatus::ERR_550);
                    } else {
                        std::error_code ec;
                        auto sz = fs::file_size(*target, ec);
                        if (ec) send_reply(client_socket, FTPStatus::ERR_550);
                        else send_reply(client_socket, "213 " + std::to_string(sz) + "\r\n");
                    }
                    break;
                }
 
                // -------- Kênh dữ liệu: thoả thuận địa chỉ/port --------
                case FtpCommand::PORT: {
                    std::string ip; int port;
                    if (!parsePortArg(parsed.arg, ip, port)) {
                        send_reply(client_socket, FTPStatus::ERR_501);
                    } else {
                        session.dataMode = DataConnMode::ACTIVE;
                        session.dataIp = ip;
                        session.dataPort = port;
                        log_info("Client requested ACTIVE mode -> " + ip + ":" + std::to_string(port));
                        send_reply(client_socket, FTPStatus::OK_200);
                    }
                    break;
                }
 
                case FtpCommand::PASV: {
                    // TODO (tích hợp với Thành viên A): thay vì chỉ CHỌN số port,
                    // ở bước sau cần thật sự bind() 1 UDP socket tại đây và lưu fd
                    // vào session để tầng RDT dùng khi client bắt đầu RETR/STOR.
                    static std::mt19937 rng(std::random_device{}());
                    static std::uniform_int_distribution<int> distPort(50000, 51000);
                    int chosenPort = distPort(rng);
                    std::string ip = getLocalIp(client_socket);
 
                    session.dataMode = DataConnMode::PASSIVE;
                    session.dataIp = ip;
                    session.dataPort = chosenPort;
                    log_info("Server entering PASSIVE mode -> " + ip + ":" + std::to_string(chosenPort));
                    send_reply(client_socket, formatPasvReply(ip, chosenPort));
                    break;
                }
 
                // -------- Truyền file thật: CHƯA cài, cần tầng RDT/UDP --------
                case FtpCommand::LIST:
                case FtpCommand::NLST:
                case FtpCommand::RETR:
                case FtpCommand::STOR:
                case FtpCommand::STOU:
                case FtpCommand::APPE: {
                    if (session.dataMode == DataConnMode::NONE) {
                        send_reply(client_socket, FTPStatus::ERR_425); // chưa PORT/PASV
                    } else {
                        // Đợi tầng RDT (rdt_packet.h + logic gửi/nhận UDP) hoàn thiện
                        send_reply(client_socket, FTPStatus::ERR_502);
                    }
                    break;
                }
 
                default: {
                    send_reply(client_socket, FTPStatus::ERR_500);
                    break;
                }
            }
            break;
        }
    }
    (void)needsAuth; //dập cảnh báo của compiler khi biến chưa được sử dụng.
}

int main()
{
    init_sockets();
    log_info("Starting Hybrid FTP Server on Port " + std::to_string(SERVER_PORT) + "...");

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
    address.sin_family = AF_INET;         // IPv4
    address.sin_addr.s_addr = INADDR_ANY; // Chấp nhận mọi IP
    address.sin_port = htons(SERVER_PORT);       // Cổng 2121

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

        log_info("New client connected!");

        ClientSession session;
        session.controlSocketFd = static_cast<int>(client_socket);

        // Gửi thông báo sẵn sàng tới Client: 220 service ready
        send_reply(client_socket, FTPStatus::OK_220);

        std::string recv_buffer = ""; // Bộ đệm tích lũy sống suốt phiên kết nối
        char buffer[BUFFER_SIZE];
        bool is_quit = false;

        // Vòng lặp nhận dữ liệu từ Client hiện tại
        while (!is_quit)
        {
            int bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);

            if (bytes_read <= 0)
            {
                log_info("Client disconnected.");
                break;
            }

            // 1. Nối dữ liệu vừa đọc được vào bộ đệm tích lũy
            buffer[bytes_read] = '\0';
            recv_buffer.append(buffer, bytes_read);

            // 2. Tìm ký tự xuống dòng '\n' trong bộ đệm
            size_t pos;
            while ((pos = recv_buffer.find('\n')) != std::string::npos)
            {
                // Tách lấy đúng 1 dòng lệnh hoàn chỉnh
                std::string command_line = recv_buffer.substr(0, pos);

                // Xóa dòng lệnh vừa lấy ra khỏi bộ đệm
                recv_buffer.erase(0, pos + 1);

                // Lọc bỏ ký tự '\r' nếu client gửi theo chuẩn Telnet/FTP (\r\n)
                if (!command_line.empty() && command_line.back() == '\r')
                {
                    command_line.pop_back();
                }

                // Nếu dòng không rỗng thì mới mang đi xử lý
                if (!command_line.empty())
                {
                    handle_client_command(client_socket, session, command_line);

                    // Kiểm tra lệnh QUIT
                    if (command_line.rfind("QUIT", 0) == 0)
                    {
                        is_quit = true;
                        break;
                    }
                }
            }
        } // Kết thúc vòng lặp xử lý 1 Client

        // Đóng kết nối với Client hiện tại SAU KHỊ Client ngắt hoặc QUIT
        closesocket(client_socket);
        log_info("Closed client connection.");
    } // Kết thúc vòng lặp accept (Server tiếp tục chờ Client tiếp theo)

    // Đóng Server socket khi dừng Server
    closesocket(server_fd);
    cleanup_sockets();
    return 0;
}