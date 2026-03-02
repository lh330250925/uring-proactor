#pragma once
#include <string>
#include <chrono>
#include <ctime>
#include <unordered_map>

// Timestamp class to generate timestamps fmt: [YYYY-MM-DD HH:MM:SS]
const std::unordered_map<std::string, std::string> get_current_timestamp();