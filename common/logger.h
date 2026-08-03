#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <string>

inline void log_info(const std::string& msg) {
    std::cout << "\033[32m[INFO]\033[0m " << msg << std::endl;
}

inline void log_error(const std::string& msg) {
    std::cerr << "\033[31m[ERROR]\033[0m " << msg << std::endl;
}

inline void log_debug(const std::string& msg) {
    std::cout << "\033[34m[DEBUG]\033[0m " << msg << std::endl;
}

#endif // LOGGER_H