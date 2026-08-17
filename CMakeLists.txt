cmake_minimum_required(VERSION 3.20)
# cmake -S . -B build -G Ninja
project(HybridFTP
    VERSION 1.0
    LANGUAGES CXX
)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


set(COMMON_HEADERS
    common/file_metadata.h
    common/logger.h
    common/protocol.h
    common/rdt_packet.h
    common/sha256.h
)

set(COMMON_SOURCES
    common/sha256.cpp
)

set(SERVER_FEATURE_SOURCES
    server/authentication_manager.cpp
    server/session_registry.cpp
)

set(SERVER_FEATURE_HEADERS
    server/authentication_manager.h
    server/session_registry.h
)

set(NETWORK_SOURCES
    network/data_channel.cpp
    network/file_receiver.cpp
    network/file_sender.cpp
    network/packet_builder.cpp
    network/packet_parser.cpp
    network/rdt_receiver.cpp
    network/rdt_sender.cpp
    network/udp_socket.cpp
)

set(NETWORK_HEADERS
    network/data_channel.h
    network/data_channel_config.h
    network/file_receiver.h
    network/file_sender.h
    network/packet_builder.h
    network/packet_parser.h
    network/rdt_receiver.h
    network/rdt_sender.h
    network/tcp_server.h
    network/transfer_session.h
    network/udp_socket.h
)



add_executable(ftp_server
    server/main_server.cpp
    ${SERVER_FEATURE_SOURCES}
    ${SERVER_FEATURE_HEADERS}
    ${COMMON_SOURCES}

    ${NETWORK_SOURCES}
    ${NETWORK_HEADERS}
    ${COMMON_HEADERS}
)

target_include_directories(ftp_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(ftp_client
    client/main_client.cpp
    ${COMMON_SOURCES}

    ${NETWORK_SOURCES}
    ${NETWORK_HEADERS}
    ${COMMON_HEADERS}
)

target_include_directories(ftp_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)

    target_link_libraries(ftp_server PRIVATE
        ws2_32
    )

    target_link_libraries(ftp_client PRIVATE
        ws2_32
    )

endif()


set_target_properties(ftp_server ftp_client
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin"
)

enable_testing()

add_executable(protocol_tests
    tests/protocol_tests.cpp
    server/authentication_manager.cpp
    common/sha256.cpp
)
target_include_directories(protocol_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME protocol_tests COMMAND protocol_tests)

add_executable(concurrency_tests
    tests/concurrency_tests.cpp
    server/session_registry.cpp
)
target_include_directories(concurrency_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME concurrency_tests COMMAND concurrency_tests)