#pragma once
#include <string>
#include <chrono>

enum class LogLevel {LEVEL, INFO, WARN, ERROR, FATAL};

struct LogEntry {
std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string source;
    std::string message;
    
    // Парсинг строки [2024-01-15 10:30:45][ERROR][DB] Timeout
    static std::optional<LogEntry> parse(const std::string& line);
};