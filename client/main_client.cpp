#include <iostream>
#include <cstring>
#include <string>
#include <filesystem>
#include <optional>
#include <random>
#include <thread>
#include <chrono>
#include <vector>
#include <sstream>
#include <fstream>
<<<<<<< Updated upstream
#include <atomic>
=======

>>>>>>> Stashed changes
#include "../common/logger.h"
#include "../common/protocol.h"
#include "../common/sha256.h"
#include "../common/socket_platform.h"

#include "../network/data_channel.h"
#include "../network/data_channel_config.h"

#define BUFFER_SIZE 1024
namespace fs = std::filesystem;

const fs::path CLIENT_ROOT = "client_files";

bool init_sockets()
{
    return
        SocketPlatform::initialize();
}

void cleanup_sockets()
{
    SocketPlatform::cleanup();
}

// Trạng thái kênh dữ liệu ở Client
enum class Mode
{
    NONE,
    ACTIVE,
    PASSIVE
};
Mode currentMode = Mode::NONE;
std::atomic<bool> is_transferring(false);
TransferType currentType = TransferType::ASCII;

std::string pasvIp;
int pasvPort = 0;
int activePort = 0;
std::atomic<DataChannel *> g_active_channel{nullptr};

// receive_reply(): đọc ĐÚNG 1 dòng reply hoàn chỉnh từ server, dùng buffer
// tích lũy sống suốt phiên (truyền theo tham chiếu).
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
// nơi gọi tự kiểm tra mã trạng thái nếu cần.
std::string sendCommandAndGetReply(SOCKET sock, std::string &recv_buffer, const std::string &command)
{
    std::string to_send = command + "\r\n";
    send(sock, to_send.c_str(), to_send.length(), 0);
    std::string reply;
    receive_reply(sock, recv_buffer, reply);
    std::cout << reply;
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
<<<<<<< Updated upstream
        std::cout << "Cannot calculate local SHA-256 for: " << localFile << std::endl;
=======
        std::cout << "[CLIENT][ERROR] Cannot calculate local SHA-256: " << localFile << '\n';

>>>>>>> Stashed changes
        return;
    }

    const std::string hashReply = sendCommandAndGetReply(sock, recv_buffer, "HASH " + remoteFile);
    const std::string serverDigest = extractSha256FromReply(hashReply);

    if (serverDigest.empty())
    {
<<<<<<< Updated upstream
        std::cout << "Server did not return a valid SHA-256 digest." << std::endl;
        return;
    }

    std::cout << "[INTEGRITY CHECK]" << std::endl;
    std::cout << "Local  SHA-256: " << localDigest << std::endl;
    std::cout << "Server SHA-256: " << serverDigest << std::endl;
=======
        std::cout << "[CLIENT][ERROR] Server returned an invalid SHA-256 digest.\n";

        return;
    }

    std::cout << "\n========== INTEGRITY CHECK ==========\n"
              << "Local SHA-256   : " << localDigest << '\n'
              << "Remote SHA-256  : " << serverDigest << '\n';
>>>>>>> Stashed changes

    if (localDigest == serverDigest)
    {
        std::cout << "Status          : MATCH\n";
    }
    else
    {
        std::cout << "Status          : MISMATCH\n";
    }

    std::cout << "=====================================\n\n";
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
// dùng để điền vào lệnh PORT gửi cho server.
std::string getLocalIp(SOCKET sock)
{
    sockaddr_in localAddr{};
    SocketPlatform::Length len = sizeof(localAddr);
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

bool convertCRLFToLFInFile(const fs::path &srcPath, const fs::path &dstPath)
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

// ---------------------------------------------------------------------
// doPut(): upload file lên server qua PASV. Chạy trên thread tách rời để
// luồng chính vẫn có thể nhận lệnh ABOR trong lúc truyền.
// ---------------------------------------------------------------------
void doPut(SOCKET sock, std::string &recv_buffer, std::string &localPath, std::string remoteName, std::string cmdVerb = "STOR", TransferType type = TransferType::BINARY)
{
    if (!fs::exists(localPath) && fs::exists(CLIENT_ROOT / localPath))
    {
        localPath = (CLIENT_ROOT / localPath).string();
    }
    if (!fs::exists(localPath) || !fs::is_regular_file(localPath))
    {
        std::cout << "[CLIENT][ERROR] Local file not found: " << localPath << '\n';
        return;
    }
    if (remoteName.empty() && cmdVerb != "STOU")
    {
        remoteName = fs::path(localPath).filename().string();
    }     

    std::string pasvReply = sendCommandAndGetReply(sock, recv_buffer, "PASV");
    std::string serverIp;
    int serverPort;
    if (pasvReply.rfind("227", 0) != 0 || !parsePasvResponse(pasvReply, serverIp, serverPort))
    {
        std::cout << "[CLIENT][ERROR] PASV negotiation failed; upload cancelled.\n";
        return;
    }

    DataChannel *dataChannel = new DataChannel();
    DataChannelConfig cfg;
    cfg.localPort = 0;
    cfg.timeout = 5000; // 5s -- bằng với timeout phía server để tránh lệch
    cfg.maxRetry = 5;
    cfg.useGBN = true;

    if (!dataChannel->open(cfg))
    {
<<<<<<< Updated upstream
        std::cout << "Cannot open local UDP data channel." << std::endl;
        delete dataChannel;
        return;
    }

    // Gửi đúng câu lệnh STOR / STOU / APPE tùy theo yêu cầu
    std::string storReply = sendCommandAndGetReply(sock, recv_buffer, cmdVerb + " " + remoteName);
=======
        std::cout << "[CLIENT][ERROR] Cannot open local UDP data channel.\n";
        return;
    }

    std::string uploadCommand = cmdVerb;

    if (cmdVerb != "STOU")
    {
        uploadCommand += " " + remoteName;
    }

    // Gửi đúng câu lệnh STOR / STOU / APPE tùy thuộc vào yêu cầu
    std::string storReply = sendCommandAndGetReply(sock, recv_buffer, uploadCommand);
>>>>>>> Stashed changes
    if (storReply.rfind("150", 0) != 0)
    {
        dataChannel->close();
        delete dataChannel;
        return;
    }

    g_active_channel.store(dataChannel);

<<<<<<< Updated upstream
    std::thread([sock, &recv_buffer, dataChannel, localPath, remoteName, serverIp, serverPort]()
                {
                    log_info("Uploading (PASV) " + localPath + " -> " + remoteName + " ...");
=======
    if (!ok)
    {
        std::cout << "[CLIENT][ERROR] Upload failed at UDP data layer.\n";
        return;
    }
>>>>>>> Stashed changes

                    // FIX: đây là UPLOAD nên phải gọi sendFile(), không phải receiveFile().
                    bool ok = dataChannel->sendFile(localPath, serverIp, static_cast<unsigned short>(serverPort));

<<<<<<< Updated upstream
                    if (ok)
                    {
                        std::string finalReply;
                        if (receive_reply(sock, recv_buffer, finalReply))
                        {
                            std::cout << "\n" << finalReply;
                        }
                    }
                    else
                    {
                        std::cout << "\nUpload aborted or failed at UDP layer." << std::endl;
                        // KHI BỊ ABORT: không gọi receive_reply() ở đây để tránh tranh chấp
                        // với luồng chính đang xử lý ABOR.
                    }
=======
    if (receive_reply(sock, recv_buffer, finalReply))
    {
        std::cout << finalReply;
>>>>>>> Stashed changes

                    dataChannel->close();
                    delete dataChannel;
                    g_active_channel.store(nullptr);

                    std::cout << "ftp> " << std::flush;
                })
        .detach();
}

// ---------------------------------------------------------------------
// doGet(): download qua PORT (active mode). Chạy trên thread tách rời để
// hỗ trợ ABOR; xử lý ASCII/BINARY và kiểm tra tính toàn vẹn SHA-256.
// ---------------------------------------------------------------------
void doGet(SOCKET sock, std::string &recv_buffer, const std::string &remoteName, std::string localPath, TransferType type = TransferType::BINARY)
{
    fs::create_directories(CLIENT_ROOT);
    if (localPath.empty())
    {
        localPath = (CLIENT_ROOT / remoteName).string();
    }

    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<int> distPort(40000, 41000);
    int myPort = distPort(rng);
    std::string myIp = getLocalIp(sock);

    DataChannel *dataChannel = new DataChannel();
    DataChannelConfig cfg;
    cfg.localPort = static_cast<unsigned short>(myPort);
    cfg.timeout = 3000;
    cfg.maxRetry = 5;
    cfg.useGBN = true;

    if (!dataChannel->open(cfg))
    {
<<<<<<< Updated upstream
        std::cout << "Cannot open local UDP port " << myPort << " for download." << std::endl;
        delete dataChannel;
=======
        std::cout << "[CLIENT][ERROR] Cannot bind local UDP port " << myPort << " for download.\n";
>>>>>>> Stashed changes
        return;
    }

    std::string portReply = sendCommandAndGetReply(sock, recv_buffer, "PORT " + formatPortArg(myIp, myPort));
    if (portReply.rfind("200", 0) != 0)
    {
        dataChannel->close();
        delete dataChannel;
        return;
    }

    std::string retrReply = sendCommandAndGetReply(sock, recv_buffer, "RETR " + remoteName);
    if (retrReply.rfind("150", 0) != 0)
    {
        dataChannel->close();
        delete dataChannel;
        return;
    }

    g_active_channel.store(dataChannel);

<<<<<<< Updated upstream
    std::thread([sock, &recv_buffer, dataChannel, remoteName, localPath, type]()
                {
                    log_info("Downloading " + remoteName + " ...");
=======
    if (!ok)
    {
        dataChannel.close();
        std::cout << "[CLIENT][ERROR] Download failed at UDP data layer; see RDT logs above.\n";
        return;
    }
>>>>>>> Stashed changes

                    bool ok = dataChannel->receiveFile();

<<<<<<< Updated upstream
                    if (ok)
                    {
                        const TransferSession &recvInfo = dataChannel->getReceiveTransferSession();
                        fs::path receivedPath = fs::path("server_files") / recvInfo.fileName;
                        std::error_code ec;
=======
    dataChannel.close();
    std::error_code ec;
    fs::rename(receivedPath, localPath, ec);
    if (ec)
    {
        std::cout << "[CLIENT][WARN] Download completed, but the file could not be moved to: "
                  << localPath << '\n'
                  << "[CLIENT][INFO] Temporary file: " << receivedPath.string() << '\n';
    }
    else
    {
        std::cout << "[CLIENT][OK] Saved file: " << localPath << '\n';
    }
>>>>>>> Stashed changes

                        if (type == TransferType::ASCII)
                        {
                            // ASCII mode: chuyển đổi định dạng xuống dòng chuẩn
                            if (convertCRLFToLFInFile(receivedPath, localPath))
                            {
                                fs::remove(receivedPath, ec);
                                std::cout << "\nSaved (ASCII mode) to " << localPath << std::endl;
                            }
                            else
                            {
                                fs::rename(receivedPath, localPath, ec);
                                std::cout << "\nSaved to " << localPath << std::endl;
                            }
                        }
                        else
                        {
                            // BINARY mode: di chuyển file trực tiếp (xóa file đích trước nếu đã tồn tại)
                            if (fs::exists(localPath))
                            {
                                fs::remove(localPath, ec);
                            }
                            fs::rename(receivedPath, localPath, ec);
                            if (ec)
                            {
                                std::cout << "\nDownloaded but could not move to " << localPath << std::endl;
                            }
                            else
                            {
                                std::cout << "\nSaved to " << localPath << std::endl;
                            }
                        }

<<<<<<< Updated upstream
                        // CHỈ ĐỌC MÃ 226 KHI TẢI THÀNH CÔNG THỰC SỰ
                        std::string finalReply;
                        if (receive_reply(sock, recv_buffer, finalReply))
                        {
                            std::cout << finalReply;
                            if (finalReply.rfind("226", 0) == 0 && fs::exists(localPath))
                            {
                                verifyEndToEndIntegrity(sock, recv_buffer, localPath, remoteName);
                            }
                        }
                    }
                    else
                    {
                        std::cout << "\nDownload aborted or failed at UDP layer." << std::endl;
                        // KHI BỊ ABORT: không gọi receive_reply() ở đây để tránh tranh chấp
                        // với luồng chính.
                    }
=======
    if (receive_reply(sock, recv_buffer, finalReply))
    {
        std::cout << finalReply;
>>>>>>> Stashed changes

                    dataChannel->close();
                    delete dataChannel;
                    g_active_channel.store(nullptr);

                    std::cout << "ftp> " << std::flush;
                })
        .detach();
}

// ---------------------------------------------------------------------
// doGetViaPasv(): giống doGet() nhưng dùng PASV thay vì PORT -- gửi 1 gói
// SYN "chào hỏi" tới server ngay sau khi mở kênh dữ liệu, để server học
// được địa chỉ client trước khi bắt đầu gửi.
// ---------------------------------------------------------------------
void doGetViaPasv(SOCKET sock, std::string &recv_buffer, const std::string &remoteName, std::string localPath, TransferType type = TransferType::BINARY)
{
    fs::create_directories(CLIENT_ROOT);
    if (localPath.empty())
    {
        localPath = (CLIENT_ROOT / remoteName).string();
    }
    else if (!fs::exists(localPath) && fs::exists(CLIENT_ROOT / localPath))
    {
        localPath = (CLIENT_ROOT / localPath).string();
    }

    std::string pasvReply = sendCommandAndGetReply(sock, recv_buffer, "PASV");
    std::string serverIp;
    int serverPort;
    if (pasvReply.rfind("227", 0) != 0 || !parsePasvResponse(pasvReply, serverIp, serverPort))
    {
        std::cout << "[CLIENT][ERROR] PASV negotiation failed; download cancelled.\n";
        return;
    }

    DataChannel *dataChannel = new DataChannel();
    DataChannelConfig cfg;
    cfg.localPort = 0;
    cfg.timeout = 3000;
    cfg.maxRetry = 5;
    cfg.useGBN = true;

    if (!dataChannel->open(cfg))
    {
<<<<<<< Updated upstream
        std::cout << "Cannot open local UDP data channel." << std::endl;
        delete dataChannel;
        return;
    }

    std::string retrReply = sendCommandAndGetReply(sock, recv_buffer, "RETR " + remoteName);
=======
        std::cout << "[CLIENT][ERROR] Cannot open local UDP data channel.\n";
        return;
    }

    std::string to_send = "RETR " + remoteName + "\r\n";
    send(sock, to_send.c_str(), to_send.length(), 0);
    std::string retrReply;
    receive_reply(sock, recv_buffer, retrReply);
    std::cout << retrReply;
>>>>>>> Stashed changes
    if (retrReply.rfind("150", 0) != 0)
    {
        dataChannel->close();
        delete dataChannel;
        return;
    }

    // Gửi gói SYN "chào hỏi" tới server NGAY SAU KHI server đã trả 150 --
    // server đang chờ đúng gói này trong receiveHandshake().
    dataChannel->sendHandshake(serverIp, static_cast<unsigned short>(serverPort));

    g_active_channel.store(dataChannel);

<<<<<<< Updated upstream
    std::thread([sock, &recv_buffer, dataChannel, remoteName, localPath, type]()
                {
                    log_info("Downloading (PASV) " + remoteName + " ...");
=======
    if (!ok)
    {
        dataChannel.close();
        std::cout << "[CLIENT][ERROR] Download failed at UDP data layer; see RDT logs above.\n";
        return;
    }
>>>>>>> Stashed changes

                    bool ok = dataChannel->receiveFile();

<<<<<<< Updated upstream
                    if (ok)
                    {
                        const TransferSession &recvInfo = dataChannel->getReceiveTransferSession();
                        fs::path receivedPath = fs::path("server_files") / recvInfo.fileName;
                        std::error_code ec;
=======
    std::error_code ec;
    fs::rename(receivedPath, localPath, ec);
    if (ec)
    {
        std::cout << "[CLIENT][WARN] Download completed, but the file could not be moved to: " << localPath << '\n';
    }
    else
    {
        std::cout << "[CLIENT][OK] Saved file: " << localPath << '\n';
    }
>>>>>>> Stashed changes

                        if (type == TransferType::ASCII)
                        {
                            if (convertCRLFToLFInFile(receivedPath, localPath))
                            {
                                fs::remove(receivedPath, ec);
                                std::cout << "\nSaved (ASCII mode) to " << localPath << std::endl;
                            }
                            else
                            {
                                fs::rename(receivedPath, localPath, ec);
                                std::cout << "\nSaved to " << localPath << std::endl;
                            }
                        }
                        else
                        {
                            if (fs::exists(localPath))
                            {
                                fs::remove(localPath, ec);
                            }
                            fs::rename(receivedPath, localPath, ec);
                            if (ec)
                            {
                                std::cout << "\nDownloaded but could not move to " << localPath << std::endl;
                            }
                            else
                            {
                                std::cout << "\nSaved to " << localPath << std::endl;
                            }
                        }

<<<<<<< Updated upstream
                        std::string finalReply;
                        if (receive_reply(sock, recv_buffer, finalReply))
                        {
                            std::cout << finalReply;
                            if (finalReply.rfind("226", 0) == 0 && fs::exists(localPath))
                            {
                                verifyEndToEndIntegrity(sock, recv_buffer, localPath, remoteName);
                            }
                        }
                    }
                    else
                    {
                        std::cout << "\nDownload aborted or failed at UDP layer." << std::endl;
                    }
=======
    if (receive_reply(sock, recv_buffer, finalReply))
    {
        std::cout << finalReply;
>>>>>>> Stashed changes

                    dataChannel->close();
                    delete dataChannel;
                    g_active_channel.store(nullptr);

                    std::cout << "ftp> " << std::flush;
                })
        .detach();
}

// ---------------------------------------------------------------------
// doList(): LIST/NLST qua PASV. Chạy đồng bộ (không cần ABOR vì thư mục
// thường nhỏ và nhanh).
// ---------------------------------------------------------------------
void doList(SOCKET sock, std::string &recv_buffer, const std::string &remoteDir, std::string cmdVerb = "LIST")
{
    std::string pasvReply = sendCommandAndGetReply(sock, recv_buffer, "PASV");
    std::string serverIp;
    int serverPort;
    if (pasvReply.rfind("227", 0) != 0 || !parsePasvResponse(pasvReply, serverIp, serverPort))
    {
        std::cout << "[CLIENT][ERROR] PASV negotiation failed; directory listing cancelled.\n";
        return;
    }

    DataChannel dataChannel;
    DataChannelConfig cfg;
    cfg.localPort = 0;
    cfg.timeout = 3000;
    cfg.maxRetry = 5;
    cfg.useGBN = true;
    if (!dataChannel.open(cfg))
    {
        std::cout << "[CLIENT][ERROR] Cannot open local UDP data channel.\n";
        return;
    }

    std::string listCmd = cmdVerb + (remoteDir.empty() ? "" : (" " + remoteDir));
    std::string to_send = listCmd + "\r\n";
    send(sock, to_send.c_str(), to_send.length(), 0);
    std::string listReply;
    receive_reply(sock, recv_buffer, listReply);
    std::cout << listReply;
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
        std::cout << "[CLIENT][ERROR] " << cmdVerb << " failed at UDP data layer.\n";
        return;
    }

    const TransferSession &recvInfo = dataChannel.getReceiveTransferSession();
    fs::path receivedPath = fs::path("server_files") / recvInfo.fileName;
    dataChannel.close();

    std::ifstream f(receivedPath);
    std::cout << "\n========== DIRECTORY LISTING ==========\n";
    std::cout << f.rdbuf();
    std::cout << "=======================================\n";
    f.close();
    std::error_code ec;
    fs::remove(receivedPath, ec);

    std::string finalReply;
    if (receive_reply(sock, recv_buffer, finalReply))
    {
        std::cout << finalReply;
    }
}

int main(int argc, char *argv[])
{
    fs::create_directories("client_files");
    init_sockets();

    std::string server_ip;
    if (argc >= 2)
    {
        server_ip = argv[1];
    }
    else
    {
        std::cout << "Server IPv4 address (e.g. 127.0.0.1): ";
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
        SocketPlatform::close(sock);
        cleanup_sockets();
        return 1;
    }

    log_info("Connecting to " + server_ip + ":" + std::to_string(server_port) + "...");
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        log_error("Connection failed. Is the server running?");
        SocketPlatform::close(sock);
        cleanup_sockets();
        return 1;
    }
    log_info("Connected!");

    std::string recv_buffer;

    std::string greeting;
    if (receive_reply(sock, recv_buffer, greeting))
    {
        std::cout << greeting;
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

<<<<<<< Updated upstream
        if (g_active_channel != nullptr && upperVerb != "ABOR")
        {
            log_error("Transfer in progress. Only ABOR is allowed.");
            continue;
        }

        if (upperVerb == "ABOR")
        {
            DataChannel *ch = g_active_channel.load();
            if (ch != nullptr)
            {
                // Bước 1: Gửi ABOR lên server
                std::string to_send = "ABOR\r\n";
                send(sock, to_send.c_str(), to_send.length(), 0);

                // Bước 2: Set cờ hủy trên DataChannel
                ch->abortTransfer();

                // Bước 3: Đợi transfer thread kết thúc (tối đa 3 giây)
                log_info("Waiting for transfer thread to finish...");
                for (int i = 0; i < 60 && g_active_channel.load() != nullptr; i++)
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));

                if (g_active_channel.load() != nullptr)
                {
                    log_error("Transfer thread did not finish in time.");
                }

                // Bước 4: Luồng chính đọc 2 reply (426 và 226)
                std::string reply1, reply2;
                if (receive_reply(sock, recv_buffer, reply1))
                    std::cout << reply1;
                if (receive_reply(sock, recv_buffer, reply2))
                    std::cout << reply2;

                log_info("ABOR complete.");
            }
            else
            {
                std::cout << "No transfer in progress." << std::endl;
            }
            continue;
        }

        if (upperVerb == "PUT")
        {
            std::string localPath, remoteName;
            iss >> localPath >> remoteName;
            if (localPath.empty())
            {
                std::cout << "Usage: put <local_file> [remote_name]" << std::endl;
                continue;
            }
            doPut(sock, recv_buffer, localPath, remoteName, "STOR", currentType);
            continue;
        }

=======
        if (upperVerb == "PUT")
        {
            std::string localPath, remoteName;
            iss >> localPath >> remoteName;
            if (localPath.empty())
            {
                std::cout << "[CLIENT][USAGE] PUT <local_file> [remote_name]\n";
                continue;
            }
            doPut(sock, recv_buffer, localPath, remoteName);
            continue;
        }

>>>>>>> Stashed changes
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
<<<<<<< Updated upstream
                std::cout << "Usage: get <remote_file> [local_name]" << std::endl;
                continue;
            }
            doGet(sock, recv_buffer, remoteName, localPath, currentType);
=======
                std::cout << "[CLIENT][USAGE] GET <remote_file> [local_name]\n";
                continue;
            }
            doGet(sock, recv_buffer, remoteName, localPath);
>>>>>>> Stashed changes
            continue;
        }

        if (upperVerb == "GETP")
<<<<<<< Updated upstream
        {
=======
            {
>>>>>>> Stashed changes
            std::string remoteName, localPath;
            iss >> remoteName >> localPath;
            if (remoteName.empty())
            {
<<<<<<< Updated upstream
                std::cout << "Usage: getp <remote_file> [local_name]" << std::endl;
                continue;
            }
            doGetViaPasv(sock, recv_buffer, remoteName, localPath, currentType);
=======
                std::cout << "[CLIENT][USAGE] GETP <remote_file> [local_name]\n";
                continue;
            }
            doGetViaPasv(sock, recv_buffer, remoteName, localPath);
>>>>>>> Stashed changes
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
<<<<<<< Updated upstream
                std::cout << "Usage: TYPE A (ASCII) or TYPE I (Binary)" << std::endl;
                continue;
            }

            std::string reply = sendCommandAndGetReply(sock, recv_buffer, "TYPE " + upperArg);
            // std::cout << reply;
            //sieuuuu

=======
                std::cout << "[CLIENT][USAGE] TYPE A | TYPE I\n";
                continue;
            }

            // Gửi lệnh TYPE tới Server và nhận phản hồi
            std::string reply = sendCommandAndGetReply(sock, recv_buffer, "TYPE " + upperArg);
            std::cout << reply;

            // Nếu Server xác nhận thành công (mã 200), cập nhật trạng thái ở Client (nếu có biến lưu currentType)
>>>>>>> Stashed changes
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
<<<<<<< Updated upstream
                doGetViaPasv(sock, recv_buffer, arg, "", currentType);
            }
            else
            {
                doGet(sock, recv_buffer, arg, "", currentType);
=======
                doGetViaPasv(sock, recv_buffer, arg, "");
            }
            else
            {
                doGet(sock, recv_buffer, arg, "");
>>>>>>> Stashed changes
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
<<<<<<< Updated upstream
                std::cout << "Usage: " << verb << " <filename>" << std::endl;
                continue;
            }
            doPut(sock, recv_buffer, arg, "", upperVerb, currentType);
            currentMode = Mode::NONE;
            continue;
        }

=======
                std::cout << "[CLIENT][USAGE] " << upperVerb << " <filename>\n";
                continue;
            }
            // Truyền đúng tên lệnh STOR / STOU / APPE vào doPut
            doPut(sock, recv_buffer, arg, "", upperVerb);
            currentMode = Mode::NONE;
            continue;
        }
>>>>>>> Stashed changes
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
<<<<<<< Updated upstream
        std::cout << reply << std::endl;

        if (upperVerb == "QUIT")
            break;
    }

    closesocket(sock);
=======
        std::cout << reply;

        if (upperVerb == "QUIT")
            break;

    }
    SocketPlatform::close(sock);
>>>>>>> Stashed changes
    cleanup_sockets();

    return 0;
}