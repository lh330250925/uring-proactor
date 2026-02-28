#include "timestamp.hpp"

std::string getCurrentTimestamp(){
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_r(&in_time_t, &buf);
    char timestamp[22];
    if (strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S]", &buf) == 0) {
        return "[0000-00-00 00:00:00]";
    }
    return std::string(timestamp);
}