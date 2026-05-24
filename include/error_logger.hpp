// error_logger.hpp
#pragma once

#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <iostream>

class ErrorLogger {
private:
    std::ofstream error_file;
    std::mutex mtx;
    
public:
    ErrorLogger(const std::string& filename) {
        error_file.open(filename, std::ios::app);
        if (!error_file.is_open()) {
            std::cerr << "Warning: Could not open error log file: " << filename << std::endl;
        }
    }
    
    ~ErrorLogger() {
        if (error_file.is_open()) {
            error_file.close();
        }
    }
    
    void log_error(const std::string& error_msg) {
        std::lock_guard<std::mutex> lock(mtx);
        if (error_file.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            error_file << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") 
                      << " ERROR: " << error_msg << std::endl;
            error_file.flush();
        }
    }
    
    void log_exception(const std::exception& e, const std::string& context) {
        log_error("Exception in " + context + ": " + e.what());
    }
};