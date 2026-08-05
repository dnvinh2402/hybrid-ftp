cmake_minimum_required(VERSION 3.10)
project(HybridFTP CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(common)

# Build Executable cho Server
add_executable(ftp_server server/main_server.cpp)

# Build Executable cho Client
add_executable(ftp_client client/main_client.cpp)

find_package(Threads REQUIRED)
target_link_libraries(ftp_server PRIVATE Threads::Threads)