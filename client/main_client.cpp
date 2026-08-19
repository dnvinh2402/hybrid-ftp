#include <iostream>
#include <cstring>
#include <string>
#include <filesystem>
#include <optional>
#include <random>
#include <thread>
#include <vector>
#include <sstream>
#include <fstream>
#include "../common/logger.h"
#include "../common/protocol.h"
#include "../network/data_channel.h"
#include "../network/data_channel_config.h"
#include "../common/sha256.h"

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

#define BUFFER_SIZE 1024
namespace fs = std::filesystem;

const fs::path CLIENT_ROOT = "client_files";

void init_sockets()
{
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

void cleanup_sockets()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

// Trạng thái kênh dữ liệu ở Client
enum class Mode
{
    NONE,
    ACTIVE,
    PASSIVE
};
Mode currentMode = Mode::NONE;
TransferType currentType = TransferType::ASCII;

std::string pasvIp;
int pasvPort = 0;
int activePort = 0;

// receive_reply(): đọc ĐÚNG 1 dòng reply hoàn chỉnh từ server, dùng buffer
// tích lũy sống suốt phiên (truyền theo tham chiếu). Sửa lại so với bản
// trước: substr(0, pos) KHÔNG lấy kèm ký tự '\n'
// out_line khiến bước "xóa \r cuối dòng" bên dưới không bao giờ khớp,
// vì back() lúc đó luôn là '\n' chứ không phải '\r'.
bool receive_reply(SOCKET sock, std::string &recv_buffer, std::string &reply)
{
    reply.clear();
    char buffer[1024];
    std::string multiLineCode = ""; // Lưu mã 3 chữ số nếu gặp multi-line (ví dụ "214 ")

    while (true)
    {
        size_t pos;
        while ((pos = recv_buffer.find('\n')) != std::string::npos)
        {
            std::string line = recv_buffer.substr(0, pos);
            recv_buffer.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            reply += line + "\r\n";

            // Nếu chưa bật chế độ multi-line và dòng bắt đầu dạng "214-..."
            if (multiLineCode.empty() && line.length() >= 4 && line[3] == '-')
            {
                multiLineCode = line.substr(0, 3) + " "; // Chờ dòng kết thúc bắt đầu bằng "214 "
            }

            // Kết thúc khi:
            // 1. Là phản hồi 1 dòng đơn (single-line)
            // 2. Gặp đúng dòng cuối cùng của phản hồi multi-line
            if (multiLineCode.empty())
            {
                return true;
            }
            else if (line.length() >= 4 && line.compare(0, 4, multiLineCode) == 0)
            {
                return true;
            }
        }

        // Nếu buffer chưa đủ 1 dòng hoàn chỉnh, đọc thêm từ Socket
        int bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0)
        {
            return false;
        }
        buffer[bytes_read] = '\0';
        recv_buffer.append(buffer, bytes_read);
    }
}

// Gửi 1 lệnh FTP qua TCP rồi chờ đúng 1 dòng reply, in ra và trả về cho
// nơi gọi tự kiểm tra mã trạng thái nếu cần (dùng cho PUT/GET nội bộ,
// khác vòng lặp CLI chính).
std::string sendCommandAndGetReply(SOCKET sock, std::string &recv_buffer, const std::string &command)
{
    std::string to_send = command + "\r\n";
    send(sock, to_send.c_str(), to_send.length(), 0);
    std::string reply;
    receive_reply(sock, recv_buffer, reply);
    std::cout << reply << std::endl;
    return reply;
}

std::string extractSha256FromReply(const std::string &reply)
{
    const std::string prefix = "213 SHA256 ";

    if (reply.rfind(prefix, 0) != 0)
    {
        return "";
    }

    std::string digest = reply.substr(prefix.length());

    while (!digest.empty() &&
           (digest.back() == '\r' ||
            digest.back() == '\n'))
    {
        digest.pop_back();
    }

    return digest;
}

void verifyEndToEndIntegrity(SOCKET sock, std::string &recv_buffer, const std::string &localFile, const std::string &remoteFile)
{
    const std::string localDigest = SHA256::hashFile(localFile);

    if (localDigest.empty())
    {
        std::cout << "Cannot calculate local SHA-256 for: " << localFile << std::endl;

        return;
    }

    const std::string hashReply = sendCommandAndGetReply(sock, recv_buffer, "HASH " + remoteFile);

    const std::string serverDigest = extractSha256FromReply(hashReply);

    if (serverDigest.empty())
    {
        std::cout << "Server did not return a valid SHA-256 digest." << std::endl;

        return;
    }

    std::cout << "[INTEGRITY CHECK]" << std::endl;

    std::cout << "Local  SHA-256: " << localDigest << std::endl;

    std::cout << "Server SHA-256: " << serverDigest << std::endl;

    if (localDigest == serverDigest)
    {
        std::cout << "Result         : MATCH" << std::endl;
    }
    else
    {
        std::cout << "Result         : MISMATCH" << std::endl;
    }
}

// Bóc tách IP + port từ reply 227 của PASV: "227 ... (h1,h2,h3,h4,p1,p2)."
bool parsePasvResponse(const std::string &response, std::string &outIp, int &outPort)
{
    size_t start = response.find('(');
    size_t end = response.find(')');
    if (start == std::string::npos || end == std::string::npos)
        return false;

    std::string data = response.substr(start + 1, end - start - 1);
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(data.c_str(), "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6)
        return false;

    outIp = std::to_string(h1) + "." + std::to_string(h2) + "." + std::to_string(h3) + "." + std::to_string(h4);
    outPort = p1 * 256 + p2;
    return true;
}

// Lấy IP local của CHÍNH client (theo góc nhìn của kết nối TCP hiện tại) --
// dùng để điền vào lệnh PORT gửi cho server, y hệt cách server dùng
// getsockname() cho PASV.
std::string getLocalIp(SOCKET sock)
{
    sockaddr_in localAddr{};
    socklen_t len = sizeof(localAddr);
    getsockname(sock, (struct sockaddr *)&localAddr, &len);
    char ipBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &localAddr.sin_addr, ipBuf, sizeof(ipBuf));
    return std::string(ipBuf);
}

// Dựng chuỗi tham số PORT: "h1,h2,h3,h4,p1,p2"
std::string formatPortArg(const std::string &ip, int port)
{
    int h1, h2, h3, h4;
    sscanf(ip.c_str(), "%d.%d.%d.%d", &h1, &h2, &h3, &h4);
    int p1 = port / 256, p2 = port % 256;
    return std::to_string(h1) + "," + std::to_string(h2) + "," + std::to_string(h3) + "," +
           std::to_string(h4) + "," + std::to_string(p1) + "," + std::to_string(p2);
}

void doPut(SOCKET sock, std::string &recv_buffer, std::string &localPath, std::string remoteName, std::string cmdVerb = "STOR")
{
    if (!fs::exists(localPath) && fs::exists(CLIENT_ROOT / localPath))
    {
        localPath = (CLIENT_ROOT / localPath).string();
    }
    if (!fs::exists(localPath) || !fs::is_regular_file(localPath))
    {
        std::cout << "Local file not found: " << localPath << std::endl;
        return;
    }
    if (remoteName.empty())
        remoteName = fs::path(localPath).filename().string();

    std::string pasvReply = sendCommandAndGetReply(sock, recv_buffer, "PASV");
    std::string serverIp;
    int serverPort;
    if (pasvReply.rfind("227", 0) != 0 || !parsePasvResponse(pasvReply, serverIp, serverPort))
    {
        std::cout << "PASV failed, cannot upload." << std::endl;
        return;
    }

    DataChannel dataChannel;
    DataChannelConfig cfg;
    cfg.localPort = 0;
    cfg.timeout = 2000;
    cfg.maxRetry = 5;
    if (!dataChannel.open(cfg))
    {
        std::cout << "Cannot open local UDP data channel." << std::endl;
        return;
    }

    // Gửi đúng câu lệnh STOR / STOU / APPE tùy thuộc vào yêu cầu
    std::string storReply = sendCommandAndGetReply(sock, recv_buffer, cmdVerb + " " + remoteName);
    if (storReply.rfind("150", 0) != 0)
    {
        dataChannel.close();
        return;
    }

    log_info("Uploading (" + cmdVerb + ") " + localPath + " -> " + remoteName + " ...");
    bool ok = dataChannel.sendFile(localPath, serverIp, static_cast<unsigned short>(serverPort));
    dataChannel.close();

    if (!ok)
    {
        std::cout << "Upload failed at UDP layer." << std::endl;
        return;
    }

    std::string finalReply;

    if (receive_reply(sock, recv_buffer, finalReply))
    {
        std::cout << finalReply << std::endl;

        // Chỉ kiểm tra tự động cho STOR.
        // Với APPE, file trên server còn chứa dữ liệu cũ.
        // Với STOU, tên file thật do server tạo ra.
        if (finalReply.rfind("226", 0) == 0 &&
            cmdVerb == "STOR")
        {
            verifyEndToEndIntegrity(sock, recv_buffer, localPath, remoteName);
        }
    }
}
void doGet(SOCKET sock, std::string &recv_buffer, const std::string &remoteName, std::string localPath)
{
    fs::create_directories(CLIENT_ROOT);
    if (localPath.empty())
    {
        fs::create_directories("client");
        localPath = (fs::path("client") / remoteName).string();
    }

    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<int> distPort(40000, 41000);
    int myPort = distPort(rng);
    std::string myIp = getLocalIp(sock);

    DataChannel dataChannel;
    DataChannelConfig cfg;
    cfg.localPort = static_cast<unsigned short>(myPort);
    cfg.timeout = 3000;
    cfg.maxRetry = 5;
    if (!dataChannel.open(cfg))
    {
        std::cout << "Cannot bind local UDP port " << myPort << " for download." << std::endl;
        return;
    }

    std::string portReply = sendCommandAndGetReply(sock, recv_buffer, "PORT " + formatPortArg(myIp, myPort));
    if (portReply.rfind("200", 0) != 0)
    {
        dataChannel.close();
        return;
    }

    std::string retrReply = sendCommandAndGetReply(sock, recv_buffer, "RETR " + remoteName);
    if (retrReply.rfind("150", 0) != 0)
    {
        dataChannel.close();
        return;
    }

    log_info("Downloading " + remoteName + " ...");
    bool ok = dataChannel.receiveFile();

    if (!ok)
    {
        dataChannel.close();
        std::cout << "Download failed at UDP layer (xem log RDT phia tren)." << std::endl;
        return;
    }

    // PHẢI lấy thông tin session (tên file đã nhận) TRƯỚC khi close(),
    // vì close() sẽ delete đối tượng FileReceiver bên trong -- gọi
    // getReceiveTransferSession() SAU close() là truy cập con trỏ đã bị
    // xóa (dangling pointer), gây lỗi Segmentation fault.
    const TransferSession &recvInfo = dataChannel.getReceiveTransferSession();
    fs::path receivedPath = fs::path("server_files") / recvInfo.fileName;

    dataChannel.close();
    std::error_code ec;
    fs::rename(receivedPath, localPath, ec);
    if (ec)
    {
        std::cout << "Downloaded but could not move to " << localPath
                  << " (con o " << receivedPath.string() << ")" << std::endl;
    }
    else
    {
        std::cout << "Saved to " << localPath << std::endl;
    }

    std::string finalReply;

    if (receive_reply(sock, recv_buffer, finalReply))
    {
        std::cout << finalReply << std::endl;

        if (finalReply.rfind("226", 0) == 0 &&
            fs::exists(localPath))
        {
            verifyEndToEndIntegrity(sock, recv_buffer, localPath, remoteName);
        }
    }
}

// ---------------------------------------------------------------------
// doGetViaPasv(): giống doGet() nhưng dùng PASV thay vì PORT -- gửi 1 gói
// SYN "chào hỏi" tới server ngay sau khi mở kênh dữ liệu, để server học
// được địa chỉ client trước khi bắt đầu gửi (khớp receiveHandshake() mới
// thêm ở server).
// ---------------------------------------------------------------------
void doGetViaPasv(SOCKET sock, std::string &recv_buffer, const std::string &remoteName, std::string localPath)
{
    if (localPath.empty())
    { // SỬA ĐOẠN NÀY: Nếu localPath trống, tự gán đường dẫn vào thư mục "client/"
        fs::create_directories("client");
        localPath = (fs::path("client") / remoteName).string();
    }

    std::string pasvReply = sendCommandAndGetReply(sock, recv_buffer, "PASV");
    std::string serverIp;
    int serverPort;
    if (pasvReply.rfind("227", 0) != 0 || !parsePasvResponse(pasvReply, serverIp, serverPort))
    {
        std::cout << "PASV failed, cannot download." << std::endl;
        return;
    }

    DataChannel dataChannel;
    DataChannelConfig cfg;
    cfg.localPort = 0;
    cfg.timeout = 3000;
    cfg.maxRetry = 5;
    if (!dataChannel.open(cfg))
    {
        std::cout << "Cannot open local UDP data channel." << std::endl;
        return;
    }

    std::string to_send = "RETR " + remoteName + "\r\n";
    send(sock, to_send.c_str(), to_send.length(), 0);
    std::string retrReply;
    receive_reply(sock, recv_buffer, retrReply);
    std::cout << retrReply << std::endl;
    if (retrReply.rfind("150", 0) != 0)
    {
        dataChannel.close();
        return;
    }

    // Gửi gói SYN "chào hỏi" tới server NGAY SAU KHI server đã trả 150 --
    // server đang chờ đúng gói này trong receiveHandshake() để học địa
    // chỉ client trước khi bắt đầu gửi file thật.
    dataChannel.sendHandshake(serverIp, static_cast<unsigned short>(serverPort));

    log_info("Downloading (PASV) " + remoteName + " ...");
    bool ok = dataChannel.receiveFile();

    if (!ok)
    {
        dataChannel.close();
        std::cout << "Download failed at UDP layer (xem log RDT phia tren)." << std::endl;
        return;
    }

    const TransferSession &recvInfo = dataChannel.getReceiveTransferSession();
    fs::path receivedPath = fs::path("server_files") / recvInfo.fileName;
    dataChannel.close();

    std::error_code ec;
    fs::rename(receivedPath, localPath, ec);
    if (ec)
    {
        std::cout << "Downloaded but could not move to " << localPath << std::endl;
    }
    else
    {
        std::cout << "Saved to " << localPath << std::endl;
    }

    std::string finalReply;

    if (receive_reply(sock, recv_buffer, finalReply))
    {
        std::cout << finalReply << std::endl;

        if (finalReply.rfind("226", 0) == 0 &&
            fs::exists(localPath))
        {
            verifyEndToEndIntegrity(sock, recv_buffer, localPath, remoteName);
        }
    }
}

void doList(SOCKET sock, std::string &recv_buffer, const std::string &remoteDir, std::string cmdVerb = "LIST")
{
    std::string pasvReply = sendCommandAndGetReply(sock, recv_buffer, "PASV");
    std::string serverIp;
    int serverPort;
    if (pasvReply.rfind("227", 0) != 0 || !parsePasvResponse(pasvReply, serverIp, serverPort))
    {
        std::cout << "PASV failed, cannot list." << std::endl;
        return;
    }

    DataChannel dataChannel;
    DataChannelConfig cfg;
    cfg.localPort = 0;
    cfg.timeout = 3000;
    cfg.maxRetry = 5;
    if (!dataChannel.open(cfg))
    {
        std::cout << "Cannot open local UDP data channel." << std::endl;
        return;
    }

    std::string listCmd = cmdVerb + (remoteDir.empty() ? "" : (" " + remoteDir));
    std::string to_send = listCmd + "\r\n";
    send(sock, to_send.c_str(), to_send.length(), 0);
    std::string listReply;
    receive_reply(sock, recv_buffer, listReply);
    std::cout << listReply << std::endl;
    if (listReply.rfind("150", 0) != 0)
    {
        dataChannel.close();
        return;
    }

    dataChannel.sendHandshake(serverIp, static_cast<unsigned short>(serverPort));

    bool ok = dataChannel.receiveFile();
    if (!ok)
    {
        dataChannel.close();
        std::cout << cmdVerb + " failed at UDP layer." << std::endl;
        return;
    }

    const TransferSession &recvInfo = dataChannel.getReceiveTransferSession();
    fs::path receivedPath = fs::path("server_files") / recvInfo.fileName;
    dataChannel.close();

    std::ifstream f(receivedPath);
    std::cout << "--------------------------------" << std::endl;
    std::cout << f.rdbuf();
    std::cout << "--------------------------------" << std::endl;
    f.close();
    std::error_code ec;
    fs::remove(receivedPath, ec);

    std::string finalReply;
    if (receive_reply(sock, recv_buffer, finalReply))
    {
        std::cout << finalReply << std::endl;
    }
}

int main(int argc, char *argv[])
{
    init_sockets();
    fs::create_directories(CLIENT_ROOT);
    // std::string server_ip = (argc >= 2) ? argv[1] : "127.0.0.1";
    std::string server_ip;
    if (argc >= 2)
    {
        server_ip = argv[1];
    }
    else
    {
        std::cout << "Nhap dia chi IP cua Server (vi du: 192.168.1.15): ";
        std::cin >> server_ip;
        std::cin.ignore(); // Xóa ký tự newline thừa trong bộ đệm cin
    }
    int server_port = (argc >= 3) ? std::atoi(argv[2]) : 2121;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
    {
        log_error("Socket creation failed.");
        cleanup_sockets();
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0)
    {
        log_error("Invalid server IP address: " + server_ip);
        closesocket(sock);
        cleanup_sockets();
        return 1;
    }

    log_info("Connecting to " + server_ip + ":" + std::to_string(server_port) + "...");
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        log_error("Connection failed. Is the server running?");
        closesocket(sock);
        cleanup_sockets();
        return 1;
    }
    log_info("Connected!");

    std::string recv_buffer;

    std::string greeting;
    if (receive_reply(sock, recv_buffer, greeting))
    {
        std::cout << greeting << std::endl;
    }


        std::string input;
        while (true)
        {
            std::cout << "ftp> ";
            if (!std::getline(std::cin, input))
                break;
            if (input.empty())
                continue;

            std::istringstream iss(input);
            std::string verb;
            iss >> verb;
            std::string upperVerb = verb;
            for (auto &c : upperVerb)
                c = toupper(c);

            if (upperVerb == "PUT")
            {
                std::string localPath, remoteName;
                iss >> localPath >> remoteName;
                if (localPath.empty())
                {
                    std::cout << "Usage: put <local_file> [remote_name]" << std::endl;
                    continue;
                }
                doPut(sock, recv_buffer, localPath, remoteName);
                continue;
            }

            if (upperVerb == "LS")
            {
                std::string dir;
                iss >> dir;
                doList(sock, recv_buffer, dir);
                continue;
            }

            if (upperVerb == "GET")
            {
                std::string remoteName, localPath;
                iss >> remoteName >> localPath;
                if (remoteName.empty())
                {
                    std::cout << "Usage: get <remote_file> [local_name]" << std::endl;
                    continue;
                }
                doGet(sock, recv_buffer, remoteName, localPath);
                continue;
            }

            if (upperVerb == "GETP")
            {
                std::string remoteName, localPath;
                iss >> remoteName >> localPath;
                if (remoteName.empty())
                {
                    std::cout << "Usage: getp <remote_file> [local_name]" << std::endl;
                    continue;
                }
                doGetViaPasv(sock, recv_buffer, remoteName, localPath);
                continue;
            }

            if (upperVerb == "PASV")
            {
                std::string reply = sendCommandAndGetReply(sock, recv_buffer, "PASV");
                if (reply.rfind("227", 0) == 0 && parsePasvResponse(reply, pasvIp, pasvPort))
                {
                    currentMode = Mode::PASSIVE;
                }
                continue;
            }

            if (upperVerb == "TYPE")
            {
                std::string arg;
                iss >> arg;
                std::string upperArg = arg;
                for (auto &c : upperArg)
                    c = toupper(c);

                if (upperArg != "A" && upperArg != "I")
                {
                    std::cout << "Usage: TYPE A (ASCII) or TYPE I (Binary)" << std::endl;
                    continue;
                }

                // Gửi lệnh TYPE tới Server và nhận phản hồi
                std::string reply = sendCommandAndGetReply(sock, recv_buffer, "TYPE " + upperArg);
                std::cout << reply;

                // Nếu Server xác nhận thành công (mã 200), cập nhật trạng thái ở Client (nếu có biến lưu currentType)
                if (reply.rfind("200", 0) == 0)
                {
                    currentType = (upperArg == "A") ? TransferType::ASCII : TransferType::BINARY;
                }
                continue;
            }

            if (upperVerb == "RETR")
            {
                std::string arg;
                iss >> arg;
                if (currentMode == Mode::PASSIVE)
                {
                    doGetViaPasv(sock, recv_buffer, arg, "");
                }
                else
                {
                    doGet(sock, recv_buffer, arg, "");
                }
                currentMode = Mode::NONE;
                continue;
            }

            if (upperVerb == "LIST" || upperVerb == "NLST")
            {
                std::string arg;
                iss >> arg;
                doList(sock, recv_buffer, arg, upperVerb); // Truyền đúng LIST hoặc NLST
                currentMode = Mode::NONE;
                continue;
            }

            if (upperVerb == "STOR" || upperVerb == "STOU" || upperVerb == "APPE")
            {
                std::string arg;
                iss >> arg;
                if (arg.empty())
                {
                    std::cout << "Usage: " << verb << " <filename>" << std::endl;
                    continue;
                }
                // Truyền đúng tên lệnh STOR / STOU / APPE vào doPut
                doPut(sock, recv_buffer, arg, "", upperVerb);
                currentMode = Mode::NONE;
                continue;
            }
            std::string to_send = input + "\r\n";
            if (send(sock, to_send.c_str(), to_send.length(), 0) == SOCKET_ERROR)
            {
                log_error("Failed to send command. Connection may be lost.");
                break;
            }

            std::string reply;
            if (!receive_reply(sock, recv_buffer, reply))
            {
                log_error("Server closed the connection");
                break;
            }
            std::cout << reply << std::endl;

            if (upperVerb == "QUIT")
                break;
        }
    closesocket(sock);
    cleanup_sockets();
    return 0;
}