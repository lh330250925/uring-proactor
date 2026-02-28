#pragma once
#include <mutex>
#include <string>
#include "timestamp.hpp"

class Logger
{
private:
    enum class LogLevel
    {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };
    static std::mutex mtx;
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger();
    ~Logger();
public:
    static Logger &getInstance();
    void log(LogLevel level, const std::string &message);
};