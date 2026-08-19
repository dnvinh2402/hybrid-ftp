#include "logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace
{
std::mutex g_logMutex;
std::ofstream g_logFile;

std::string timestampNow()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};

#ifdef _WIN32
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif

    std::ostringstream output;
    output << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

void writeMessage(
    const char *level,
    const std::string &message,
    std::ostream &terminal)
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    const std::string prefix =
        "[" + timestampNow() + "] [" + level + "] ";

    terminal << prefix << message << std::endl;

    if (g_logFile.is_open())
    {
        g_logFile << prefix << message << '\n';
        g_logFile.flush();
    }
}
}

namespace Logger
{
bool initialize(
    const std::string &filePath,
    bool truncateFile)
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    if (g_logFile.is_open())
    {
        g_logFile.close();
    }

    const std::filesystem::path path(filePath);

    if (path.has_parent_path())
    {
        std::error_code errorCode;
        std::filesystem::create_directories(
            path.parent_path(),
            errorCode);

        if (errorCode)
        {
            return false;
        }
    }

    std::ios::openmode mode = std::ios::out;
    mode |= truncateFile ? std::ios::trunc : std::ios::app;

    g_logFile.open(filePath, mode);
    return g_logFile.is_open();
}

void shutdown()
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    if (g_logFile.is_open())
    {
        g_logFile.flush();
        g_logFile.close();
    }
}

void info(const std::string &message)
{
    writeMessage("INFO", message, std::cout);
}

void error(const std::string &message)
{
    writeMessage("ERROR", message, std::cerr);
}

void debug(const std::string &message)
{
    writeMessage("DEBUG", message, std::cout);
}
}

void log_info(const std::string &message)
{
    Logger::info(message);
}

void log_error(const std::string &message)
{
    Logger::error(message);
}

void log_debug(const std::string &message)
{
    Logger::debug(message);
}