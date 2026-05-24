// anomaly_detector.hpp
#pragma once

#include <deque>
#include <mutex>
#include <chrono>
#include <vector>
#include <string>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cmath>

struct AnomalyReport {
    std::chrono::system_clock::time_point timestamp;
    std::string type;
    std::string description;
    double value;
    double threshold;
    
    std::string to_string() const {
        std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
        std::tm* tm = std::localtime(&time);
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S") 
            << " | " << type << " | " << description 
            << " | Value: " << value << " | Threshold: " << threshold;
        return oss.str();
    }
    
    std::string to_json() const {
        std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
        std::ostringstream oss;
        oss << "  {\n"
            << "    \"timestamp\": \"" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << "\",\n"
            << "    \"type\": \"" << type << "\",\n"
            << "    \"description\": \"" << description << "\",\n"
            << "    \"value\": " << value << ",\n"
            << "    \"threshold\": " << threshold << "\n"
            << "  }";
        return oss.str();
    }
};

class AnomalyDetector {
private:
    std::deque<double> error_rate_window;
    std::deque<double> throughput_window;
    std::vector<AnomalyReport> anomalies;
    std::mutex mtx;
    size_t window_size = 60; // 60 секунд
    
public:
    void add_metrics(double error_rate, double throughput) {
    std::lock_guard<std::mutex> lock(mtx);
    
    // Добавляем новые значения
    error_rate_window.push_back(error_rate);
    throughput_window.push_back(throughput);
    
    // Ограничиваем размер окна
    while (error_rate_window.size() > window_size) {
        error_rate_window.pop_front();
    }
    while (throughput_window.size() > window_size) {
        throughput_window.pop_front();
    }
    
    // Проверяем аномалии ТОЛЬКО если накоплено достаточно данных
    if (error_rate_window.size() >= 10) {
        detect_anomalies(error_rate, throughput);
    }
}
    
private:
    void detect_anomalies(double error_rate, double throughput) {
        // Аномалия 1: Резкий скачок ошибок (> 3 сигма)
        if (error_rate_window.size() >= 10) {
            double mean = std::accumulate(error_rate_window.begin(), error_rate_window.end(), 0.0) / error_rate_window.size();
            double variance = 0.0;
            for (double val : error_rate_window) {
                variance += (val - mean) * (val - mean);
            }
            variance /= error_rate_window.size();
            double stddev = std::sqrt(variance);
            
            if (error_rate > mean + 3 * stddev && stddev > 0.01) {
                AnomalyReport report;
                report.timestamp = std::chrono::system_clock::now();
                report.type = "ERROR_RATE_SPIKE";
                report.description = "Sudden increase in error rate detected";
                report.value = error_rate;
                report.threshold = mean + 3 * stddev;
                anomalies.push_back(report);
            }
        }
        
        // Аномалия 2: Падение производительности
        if (throughput_window.size() >= 10) {
            double mean = std::accumulate(throughput_window.begin(), throughput_window.end(), 0.0) / throughput_window.size();
            
            if (throughput < mean * 0.5) { // Падение на 50% и более
                AnomalyReport report;
                report.timestamp = std::chrono::system_clock::now();
                report.type = "THROUGHPUT_DROP";
                report.description = "Significant throughput degradation detected";
                report.value = throughput;
                report.threshold = mean * 0.5;
                anomalies.push_back(report);
            }
        }
    }
    
public:
    std::vector<AnomalyReport> get_recent_anomalies(size_t count = 10) {
        std::lock_guard<std::mutex> lock(mtx);
        size_t start = anomalies.size() > count ? anomalies.size() - count : 0;
        return std::vector<AnomalyReport>(anomalies.begin() + start, anomalies.end());
    }
    
    void export_to_json(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mtx);
        std::ofstream file(filename);
        if (file.is_open()) {
            file << "{\n  \"anomalies\": [\n";
            for (size_t i = 0; i < anomalies.size(); ++i) {
                file << anomalies[i].to_json();
                if (i < anomalies.size() - 1) file << ",";
                file << "\n";
            }
            file << "  ]\n}\n";
            file.close();
        }
    }
    
    void export_to_csv(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mtx);
        std::ofstream file(filename);
        if (file.is_open()) {
            file << "Timestamp,Type,Description,Value,Threshold\n";
            for (const auto& anomaly : anomalies) {
                std::time_t time = std::chrono::system_clock::to_time_t(anomaly.timestamp);
                std::tm* tm = std::localtime(&time);
                file << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << ","
                     << anomaly.type << ","
                     << "\"" << anomaly.description << "\","
                     << anomaly.value << ","
                     << anomaly.threshold << "\n";
            }
            file.close();
        }
    }
    
    void print_anomalies() {
        std::lock_guard<std::mutex> lock(mtx);
        if (anomalies.empty()) {
            std::cout << "[Thread C] No anomalies detected.\n";
        } else {
            std::cout << "\n=== [Thread C] ANOMALIES DETECTED ===\n";
            for (const auto& anomaly : anomalies) {
                std::cout << anomaly.to_string() << "\n";
            }
            std::cout << "==========================\n\n";
        }
    }
};