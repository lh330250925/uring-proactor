#include "timestamp.hpp"
const std::unordered_map<std::string, std::string> get_current_timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::unordered_map<std::string, std::string> timestamp_map;
    std::tm buf;
    localtime_r(&in_time_t, &buf);
    char timestamp[22];
    strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S]", &buf);
    timestamp_map["[YmdHMS]"] = std::string(timestamp);
    strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d]", &buf);
    timestamp_map["[Ymd]"] = std::string(timestamp);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &buf);
    timestamp_map["YmdHMS"] = std::string(timestamp);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d", &buf);
    timestamp_map["Ymd"] = std::string(timestamp);
    return timestamp_map;
}