#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <string>

enum class LogLevel {DEBUG, INFO, WARN, ERROR, FATAL};

struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string source;
    std::string message;

    // Парсинг строки [2024-01-15 10:30:45][ERROR][DB] Timeout
    static std::optional<LogEntry> parse(const std::string& line) {
        std::regex pattern(R"(\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\] \[(\w+)\] \[(\w+)\] (.+))");
        std::smatch matches;
        if (!std::regex_match(line, matches, pattern)) {
            return std::nullopt;
        }
        LogEntry entry;

        // парсинг временной метки
        std::istringstream ss(matches[1].str());
        std::tm time_struct = {};
        ss >> std::get_time(&time_struct, "%Y-%m-%d %H:%M:%S");
        if (ss.fail()) {
            return std::nullopt;
        }
        entry.timestamp = std::chrono::system_clock::from_time_t(std::mktime(&time_struct));

        // парсинг уровня
        std::string level = matches[2].str();
        if (level == "DEBUG") entry.level = LogLevel::DEBUG;
        else if (level == "INFO") entry.level = LogLevel::INFO;
        else if (level == "WARN") entry.level = LogLevel::WARN;
        else if (level == "ERROR") entry.level = LogLevel::ERROR;
        else if (level == "FATAL") entry.level = LogLevel::FATAL;
        else return std::nullopt;

        entry.source = matches[3].str();
        entry.message = matches[4].str();
        
        return entry;
    }

    std::string to_string() const {
        std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
        std::tm* tm = std::localtime(&time);
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
        
        std::string level_str;
        switch(level) {
            case LogLevel::DEBUG: level_str = "DEBUG"; break;
            case LogLevel::INFO: level_str = "INFO"; break;
            case LogLevel::WARN: level_str = "WARN"; break;
            case LogLevel::ERROR: level_str = "ERROR"; break;
            case LogLevel::FATAL: level_str = "FATAL"; break;
        }
        
        oss << " [" << level_str << "] [" << source << "] " << message;
        return oss.str();
    }
};
