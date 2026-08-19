#ifndef LOGGER_H
#define LOGGER_H

#include <string>

namespace Logger
{
// Opens the process log file. When truncateFile is true, the previous
// contents are cleared; otherwise new messages are appended.
bool initialize(const std::string &filePath, bool truncateFile = false);

// Flushes and closes the current process log file.
void shutdown();

// Writes the same message to the terminal and, when initialized, to the
// configured log file. Functions are thread-safe inside one process.
void info(const std::string &message);
void error(const std::string &message);
void debug(const std::string &message);
}

// Backward-compatible helpers used throughout the existing project.
void log_info(const std::string &message);
void log_error(const std::string &message);
void log_debug(const std::string &message);

#endif // LOGGER_H