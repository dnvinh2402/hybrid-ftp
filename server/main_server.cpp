#include <iostream>
#include <cstring>
#include <string>
#include "../common/logger.h"
#include "../common/protocol.h"

// Khai báo thư viện Socket tương thích cả Windows và Linux
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
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

#define PORT 2121
#define BUFFER_SIZE 1024

// Hàm khởi tạo thư viện Socket (bắt buộc trên Windows)
void init_sockets() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        log_error("Failed to initialize Winsock.");
        exit(EXIT_FAILURE);
    }
#endif
}

// Hàm dọn dẹp Socket khi đóng chương trình
void cleanup_sockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}


void send_reply(SOCKET client_socket, const std::string& reply) {
    send(client_socket, reply.c_str(), reply.length(), 0);
    log_debug("Sent reply: " + reply);
}

// Hàm xử lý phân tích và phản hồi lệnh FTP
void handle_client_command(SOCKET client_socket, ClientSession& session, const std::string& raw_line) {
    log_debug("Received command: " + raw_line);

    // Tách lệnh (Word đầu tiên) và Tham số (Phần còn lại)
    // std::string cmd = command_line;
    // std::string arg = "";
    // size_t space_pos = command_line.find(' ');
    // if (space_pos != std::string::npos) {
    //     cmd = command_line.substr(0, space_pos);
    //     arg = command_line.substr(space_pos + 1);
    // }

    // // Xử lý ký tự xuống dòng \r\n nếu có
    // if (!arg.empty() && arg.back() == '\r') arg.pop_back();
    // if (!arg.empty() && arg.back() == '\n') arg.pop_back();
    // if (!cmd.empty() && cmd.back() == '\r') cmd.pop_back();
    // if (!cmd.empty() && cmd.back() == '\n') cmd.pop_back();

    // std::string response;

    // // PHÂN TÍCH LỆNH (COMMAND PARSER)
    // if (cmd == "USER") {
    //     log_info("Client set username: " + arg);
    //     response = FTPStatus::NEED_PASS_331; // "331 Username OK, need password."
    // } 
    // else if (cmd == "PASS") {
    //     log_info("Client sent password. Authenticating...");
    //     response = FTPStatus::OK_230;       // "230 User logged in, proceed."
    // } 
    // else if (cmd == "PWD") {
    //     response = "257 \"/\" is current directory.\r\n";
    // } 
    // else if (cmd == "QUIT") {
    //     response = FTPStatus::OK_221;       // "221 Goodbye."
    //     send(client_socket, response.c_str(), response.length(), 0);
    //     return;
    // } 
    // else if (cmd == "NOOP") {
    //     response = FTPStatus::OK_200;       // "200 Command OK."
    // } 
    // else {
    //     response = FTPStatus::ERR_500;      // "500 Syntax error, command unrecognized."
    // }

    // // Gửi mã phản hồi TCP về cho Client
    // send(client_socket, response.c_str(), response.length(), 0);

    ParsedCommand parsed = parseLine(raw_line);

    switch (parsed.cmd) {
        case FtpCommand :: USER: {
            session.username = parsed.arg;
            log_info("Client set user: " + session.username);
            send_reply(client_socket, FTPStatus::NEED_PASS_331);
            break;
        }

        case FtpCommand::PASS:{
            //chap nhan moi mau khua mien la cos user

            if(session.username.empty()){
                send_reply(client_socket, FTPStatus::ERR_530);
            }
            else{
                session.authenticated = true;
                log_info("User '" + session.username + "' authenticated.");
                send_reply(client_socket, FTPStatus::OK_230);
            }
            break;
        }

        case FtpCommand::PWD: {
            //Tra ve thu muc dang luu trog session

            send_reply(client_socket, "257 \"" + session.currentDir + "\" is current directory.\r\n");
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

        // Các lệnh đã khai báo trong enum nhưng CHƯA cài đặt logic (TODO cho bước sau):
        // CWD, CDUP, MKD, RMD, LIST, TYPE, PORT, PASV, RETR, STOR, ...
        default: {
            send_reply(client_socket, FTPStatus::ERR_500);
            break;
        }
    }
}

int main() {
    init_sockets();
    log_info("Starting Hybrid FTP Server on Port " + std::to_string(PORT) + "...");

    // BƯỚC 1: Tạo TCP Socket
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        log_error("Socket creation failed.");
        cleanup_sockets();
        return 1;
    }

    // Thiết lập tùy chọn cho phép dùng lại cổng nhanh (SO_REUSEADDR)
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    // BƯỚC 2: Cấu hình địa chỉ IP và Port
    sockaddr_in address{};
    address.sin_family = AF_INET; //IPv4
    address.sin_addr.s_addr = INADDR_ANY; // Chấp nhận mọi IP
    address.sin_port = htons(PORT);        // Cổng 2121

    // BƯỚC 3: Gắn Socket vào Port (Bind)
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        log_error("Bind failed. Port might be in use.");
        closesocket(server_fd);
        cleanup_sockets();
        return 1;
    }

    // BƯỚC 4: Lắng nghe kết nối (Listen)
    if (listen(server_fd, 3) == SOCKET_ERROR) {
        log_error("Listen failed.");
        closesocket(server_fd);
        cleanup_sockets();
        return 1;
    }

    log_info("Server is listening on port " + std::to_string(PORT) + ". Waiting for connections...");

    // BƯỚC 5: Vòng lặp liên tục chấp nhận Client
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        
        // Chờ kết nối mới từ Client
        SOCKET client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (client_socket == INVALID_SOCKET) {
            log_error("Accept connection failed.");
            continue;
        }

        log_info("New client connected!");

        
        ClientSession session;
        session.controlSocketFd = static_cast<int>(client_socket);

        // gui thong bao san sang client 220 service ready
        send_reply(client_socket, FTPStatus::OK_220);
        
        // Đọc dữ liệu lệnh liên tục từ Client này
        char buffer[BUFFER_SIZE] = {0};
        while (true) {
            memset(buffer, 0, BUFFER_SIZE);
            int bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
            
            if (bytes_read <= 0) {
                log_info("Client disconnected.");
                break;
            }

            std::string command_line(buffer);
            handle_client_command(client_socket, session, command_line);

            // Nếu client gửi lệnh QUIT thì thoát phiên làm việc
            if (command_line.rfind("QUIT", 0) == 0) {
                break;
            }
        }

        // Đóng kết nối với Client hiện tại
        closesocket(client_socket);
    }

    // Đóng Server socket
    closesocket(server_fd);
    cleanup_sockets();
    return 0;
}