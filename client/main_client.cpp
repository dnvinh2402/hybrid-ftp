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

void cleanup_socket() {
#ifdef _WIN32
    WSACleanup();
#endif
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

int main() {
    // 1. Tạo TCP Socket & Connect tới Server (127.0.0.1:2121)
    // 2. Nhận câu chào 220 từ Server
    
    std::string userInput;
    while (true) {
        std::cout << "ftp> ";
        std::getline(std::cin, userInput);
        if (userInput.empty()) continue;
        
        // Ví dụ xử lý lệnh download file: get <filename>
        if (userInput.rfind("get ", 0) == 0) {
            std::string filename = userInput.substr(4);
            
            // BƯỚC A: Gửi PASV qua TCP để xin cổng UDP
            // send(tcp_sock, "PASV\r\n")
            // std::string res = read_tcp_line();
            
            // BƯỚC B: Lấy IP và Port UDP của Server
            std::string udpIp; int udpPort;
            // parsePasvResponse(res, udpIp, udpPort);
            
            // BƯỚC C: Gửi lệnh RETR qua TCP
            // send(tcp_sock, "RETR " + filename + "\r\n")
            
            // BƯỚC D: Dùng UDP/RDT của đồng đội để tải file về
            // rdt_recv_file(udpIp, udpPort, filename);
            
            // BƯỚC E: Đọc phản hồi hoàn tất 226 từ TCP
        }
        else if (userInput == "quit") {
            // send(tcp_sock, "QUIT\r\n");
            break;
        }
        // Xử lý các lệnh khác: ls, put, cd, pwd, mkdir...
    }
    
    // Đóng TCP Socket
    return 0;
}