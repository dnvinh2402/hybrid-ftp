#include <iostream>
#include <cstring>
#include <string>
#include <filesystem>
#include <optional>
#include <random>
#include <thread>     
#include <vector>
#include <sstream>                                    
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

#define BUFFER_SIZE 1024

void init_sockets() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

void cleanup_sockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}


bool receive_reply(SOCKET socket, std::string &recv_buffer, std::string& out_line){
    size_t pos;
    while((pos = recv_buffer.find('\n')) == std::string::npos){
        char buffer[BUFFER_SIZE];
        int bytes_read = recv(socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0) return false;

        recv_buffer.append(buffer, bytes_read);
    }

    out_line = recv_buffer.substr(0, pos + 1);
    recv_buffer.erase(0, pos + 1);
    if (!out_line.empty() && out_line.back() == '\r') out_line.pop_back();
    return true;

}
// Hàm bóc tách IP và Port từ phản hồi 227 của PASV
bool parsePasvResponse(const std::string& response, std::string& outIp, int& outPort) {
    size_t start = response.find('(');
    size_t end = response.find(')');
    if (start == std::string::npos || end == std::string::npos) return false;
    
    std::string data = response.substr(start + 1, end - start - 1);
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(data.c_str(), "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) return false;
    
    outIp = std::to_string(h1) + "." + std::to_string(h2) + "." + std::to_string(h3) + "." + std::to_string(h4);
    outPort = p1 * 256 + p2;
    return true;
}

int main(int argc, char* argv[]) {
    init_sockets();
    
    std::string server_ip = (argc >= 2) ? argv[1]: "127.0.0.1";
    int server_port = (argc >= 3) ? std::atoi(argv[2]) : 2121;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        log_error("Socket creation failed.");
        cleanup_sockets();
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0){
        log_error("Invalid server IP address: " + server_ip);
        closesocket(sock);
        cleanup_sockets();
        return 1;
    }

    log_info("Connecting to " + server_ip + ":" + std::to_string(server_port) + "...");
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR){
        log_error("Connection failed. Is the server running?");
        closesocket(sock);
        cleanup_sockets();
        return 1;
    }

    log_info("Connected!");
    std::string recv_buffer;

    // Đọc câu chào 220 đầu tiên server tự động gửi ngay sau khi connect
    std::string greeting;
    if (receive_reply(sock, recv_buffer, greeting)){
        std::cout << greeting << std::endl;
    }

    // vong lap cli
    std::string input;
    while(true){
        std::cout << "ftp> ";
        if (!std::getline(std::cin, input)) break;

        if (input.empty()) continue;


        // gui lenh len server
        std::string to_send = input + "\r\n";
        if (send(sock, to_send.c_str(), to_send.length(), 0) == SOCKET_ERROR){
            log_error("Failed to send command. Connection may be lost.");
            break;
        }

        //Nhan reply, in ra cho nguoi xem
        std::string reply;
        if (!receive_reply(sock, recv_buffer, reply)) {
            log_error("Server closed the connection");
            break;
        }

        std::cout << reply << std::endl;

         // Nếu vừa gửi QUIT thì thoát vòng lặp, đóng chương trình luôn 
        std::string upper_verb = input.substr(0, input.find(' '));
        for (auto& c : upper_verb) c = toupper(c);
        if (upper_verb == "QUIT") break;
    }

    closesocket(sock);
    cleanup_sockets();
    return 0;
}