#ifndef __NEOLUX_NXLOG_HPP__
#define __NEOLUX_NXLOG_HPP__

#include <string>
#include <mutex>
#include <fstream>

namespace neolux {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class NxLogger {
public:
    virtual ~NxLogger() = default;

    virtual void set_log_file(const std::string& filename) = 0;

    virtual void log(LogLevel level, const std::string& msg) = 0;

    // 高级封装：语义友好的方法
    void debug(const std::string& msg)   { log(LogLevel::DEBUG,   msg); }
    void info(const std::string& msg)    { log(LogLevel::INFO,    msg); }
    void warning(const std::string& msg) { log(LogLevel::WARNING, msg); }
    void error(const std::string& msg)   { log(LogLevel::ERROR,   msg); }
};

// 全局 logger 访问与设置
NxLogger& get_logger();
void set_logger(NxLogger* logger);

// 默认实现：线程安全，输出到 stdout 和文件
class DefaultLogger : public NxLogger {
public:
    DefaultLogger();
    void set_log_file(const std::string& filename) override;
    void log(LogLevel level, const std::string& msg) override;

private:
    std::string format(LogLevel level, const std::string& msg);
    std::string current_time();
    const char* level_to_string(LogLevel level);

    std::mutex mutex_;
    std::ofstream file_;
    bool to_file_ = false;
};

} // namespace neolux

#endif // __NEOLUX_NXLOG_HPP__
