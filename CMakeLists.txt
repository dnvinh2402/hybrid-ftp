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
    common/socket_platform.h
)

set(COMMON_SOURCES
    common/logger.cpp
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
    network/sliding_window_sender.cpp
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
    network/sliding_window_sender.h
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

# -----------------------------------------------------------------------------
# Unit tests
# -----------------------------------------------------------------------------

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
    common/logger.cpp
)
target_include_directories(concurrency_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME concurrency_tests COMMAND concurrency_tests)

add_executable(command_tests
    tests/command_tests.cpp
)
target_include_directories(command_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME command_tests COMMAND command_tests)

add_executable(socket_compat_tests
    tests/socket_compat_tests.cpp
    network/udp_socket.cpp
    common/logger.cpp
)
target_include_directories(socket_compat_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    target_link_libraries(socket_compat_tests PRIVATE ws2_32)
endif()

add_test(NAME socket_compat_tests COMMAND socket_compat_tests)

# -----------------------------------------------------------------------------
# Data-plane integration tests
#
# Every target below links the real DataChannel/RDT implementation so the tests
# exercise the same code that ftp_client and ftp_server use at runtime.
# -----------------------------------------------------------------------------

function(add_network_integration_test target source)
    add_executable(${target}
        ${source}
        ${COMMON_SOURCES}
        ${NETWORK_SOURCES}
        ${NETWORK_HEADERS}
        ${COMMON_HEADERS}
    )

    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(WIN32)
        target_link_libraries(${target} PRIVATE ws2_32)
    endif()

    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_network_integration_test(
    rdt_integration_tests
    tests/rdt_integration_tests.cpp
)

add_network_integration_test(
    transfer_integration_tests
    tests/transfer_integration_tests.cpp
)

add_network_integration_test(
    ascii_transfer_tests
    tests/ascii_transfer_tests.cpp
)

add_network_integration_test(
    abort_transfer_tests
    tests/abort_transfer_tests.cpp
)

add_network_integration_test(
    session_isolation_tests
    tests/session_isolation_tests.cpp
)

# -----------------------------------------------------------------------------
# Real FTP control-channel integration test
#
# The Python test starts the compiled ftp_server, connects through TCP, verifies
# authentication/state-changing commands and expected FTP reply codes, then
# terminates the server. It is optional only when Python 3 is unavailable.
# -----------------------------------------------------------------------------

find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    add_test(
        NAME ftp_command_integration
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/ftp_command_integration.py
            --server
            $<TARGET_FILE:ftp_server>
    )

    set_tests_properties(
        ftp_command_integration
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RUN_SERIAL TRUE
            TIMEOUT 30
    )
else()
    message(STATUS "Python 3 not found: ftp_command_integration will not be registered with CTest.")
endif()