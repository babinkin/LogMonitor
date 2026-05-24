// source_grouper.hpp
#pragma once

#include <map>
#include <string>
#include <mutex>
#include <vector>
#include <fstream>
#include <iomanip>

struct SourceStats {
    std::string source_name;
    size_t total_count = 0;
    size_t error_count = 0;
    size_t warning_count = 0;
    
    double error_rate() const {
        return total_count > 0 ? (static_cast<double>(error_count) / total_count) * 100 : 0;
    }
};

class SourceGrouper {
private:
    std::map<std::string, SourceStats> source_stats;
    std::mutex mtx;
    
public:
    void add_entry(const std::string& source, LogLevel level) {
        std::lock_guard<std::mutex> lock(mtx);
        auto& stats = source_stats[source];
        stats.source_name = source;
        stats.total_count++;
        
        if (level == LogLevel::ERROR || level == LogLevel::FATAL) {
            stats.error_count++;
        }
        if (level == LogLevel::WARN) {
            stats.warning_count++;
        }
    }
    
    std::vector<SourceStats> get_top_sources(size_t count = 10) {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<SourceStats> sources;
        for (const auto& [name, stats] : source_stats) {
            sources.push_back(stats);
        }
        
        std::sort(sources.begin(), sources.end(), [](const SourceStats& a, const SourceStats& b) {
            return a.total_count > b.total_count;
        });
        
        if (sources.size() > count) {
            sources.resize(count);
        }
        return sources;
    }
    
    void print_report() {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "\n=== [Thread D] STATISTICS BY SOURCE ===\n";
        size_t total_count = 0;

        std::cout << std::left << std::setw(15) << "Source" 
                  << std::setw(10) << "Total" 
                  << std::setw(10) << "Errors" 
                  << std::setw(10) << "Warnings"
                  << std::setw(12) << "Error Rate %" << "\n";
        std::cout << std::string(57, '-') << "\n";
        
        for (const auto& [name, stats] : source_stats) {
            total_count += stats.total_count;
            std::cout << std::left << std::setw(15) << name.substr(0, 14)
                      << std::setw(10) << stats.total_count
                      << std::setw(10) << stats.error_count
                      << std::setw(10) << stats.warning_count
                      << std::setw(12) << std::fixed << std::setprecision(1) 
                      << stats.error_rate() << "\n";
        }
        std::cout << "\nTotal processed by Thread D: " << total_count << "\n\n";
        std::cout << "============================\n\n";
    }
    
    void export_to_csv(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mtx);
        std::ofstream file(filename);

        if (file.is_open()) {
            file << "Source,Total Entries,Errors,Warnings,Error Rate %\n";
            for (const auto& [name, stats] : source_stats) {
                file << name << ","
                     << stats.total_count << ","
                     << stats.error_count << ","
                     << stats.warning_count << ","
                     << stats.error_rate() << "\n";
            }
            file.close();
        }
    }
};