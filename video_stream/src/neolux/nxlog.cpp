#include "neolux/nxlog.hpp"
#include <iostream>
#include <sstream>
#include <ctime>

namespace neolux {

static NxLogger* global_logger = nullptr;

NxLogger& get_logger() {
    if (!global_logger) {
        static DefaultLogger default_instance;
        global_logger = &default_instance;
    }
    return *global_logger;
}

void set_logger(NxLogger* logger) {
    global_logger = logger;
}

// -- DefaultLogger 实现 --

DefaultLogger::DefaultLogger() = default;

void DefaultLogger::set_log_file(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_.open(filename, std::ios::out | std::ios::app);
    to_file_ = file_.is_open();
}

void DefaultLogger::log(LogLevel level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string formatted = format(level, msg);
    std::cout << formatted << std::endl;
    if (to_file_) file_ << formatted << std::endl;
}

std::string DefaultLogger::format(LogLevel level, const std::string& msg) {
    std::ostringstream oss;
    oss << "[" << current_time() << "] [" << level_to_string(level) << "] " << msg;
    return oss.str();
}

std::string DefaultLogger::current_time() {
    std::time_t now = std::time(nullptr);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%F %T", std::localtime(&now));
    return buf;
}

const char* DefaultLogger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace neolux
