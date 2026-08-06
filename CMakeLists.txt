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
    network/packet_builder.cpp
)
add_executable(test_packet_builder
    tests/test_packet_builder.cpp
    network/packet_builder.cpp
)
add_executable(test_timeout
    tests/test_timeout.cpp
    network/udp_socket.cpp
)
add_executable(test_checksum
    tests/test_checksum.cpp
    network/packet_builder.cpp
)
add_executable(
    test_retransmission
    tests/test_retransmission.cpp
    network/udp_socket.cpp
    network/packet_builder.cpp
    network/rdt_sender.cpp
    network/rdt_receiver.cpp
)
add_executable(test_stop_and_wait
    tests/test_stop_and_wait.cpp
    network/udp_socket.cpp
    network/rdt_sender.cpp
    network/rdt_receiver.cpp
    network/packet_builder.cpp
)
add_executable(test_file_sender
    tests/test_file_sender.cpp
    network/file_sender.cpp
    network/packet_builder.cpp
    network/udp_socket.cpp
    network/rdt_sender.cpp
)

add_executable(test_file_transfer
    tests/test_file_transfer.cpp
    network/file_sender.cpp
    network/file_receiver.cpp
    network/packet_builder.cpp
    network/udp_socket.cpp
    network/rdt_sender.cpp
    network/rdt_receiver.cpp
)
add_executable(test_transfer_session
    tests/test_transfer_session.cpp

    network/file_sender.cpp
    network/file_receiver.cpp

    network/packet_builder.cpp
    network/udp_socket.cpp
    network/rdt_sender.cpp
    network/rdt_receiver.cpp
)
if (WIN32)
    target_link_libraries(ftp_server ws2_32)
    target_link_libraries(ftp_client ws2_32)
    target_link_libraries(test_udp_socket ws2_32)
    target_link_libraries(test_send_receive ws2_32)
    target_link_libraries(test_packet_builder ws2_32)
    target_link_libraries(test_timeout ws2_32)
    target_link_libraries(test_retransmission ws2_32)
    target_link_libraries(test_stop_and_wait ws2_32)
    target_link_libraries(test_checksum ws2_32)
    target_link_libraries(test_file_sender ws2_32)
    target_link_libraries(test_file_transfer ws2_32)
    target_link_libraries(test_transfer_session ws2_32)
endif()