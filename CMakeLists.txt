cmake_minimum_required(VERSION 3.10)
project(HybridFTP CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(common)

# Build Executable cho Server
add_executable(ftp_server server/main_server.cpp)

# Build Executable cho Client
add_executable(ftp_client client/main_client.cpp)
# Test UDP Socket
add_executable(test_udp_socket
    tests/test_udp_socket.cpp
    network/udp_socket.cpp
)
add_executable(test_send_receive
    tests/test_send_receive.cpp
    network/udp_socket.cpp
)
if (WIN32)
    target_link_libraries(ftp_server ws2_32)
    target_link_libraries(ftp_client ws2_32)
    target_link_libraries(test_udp_socket ws2_32)
    target_link_libraries(test_send_receive ws2_32)
endif()