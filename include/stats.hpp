// stats.hpp - полная версия
#pragma once

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <mutex>
#include <vector>

struct atomic_stats {
    std::atomic<size_t> total{0};
    std::atomic<size_t> errors{0};
    std::atomic<size_t> warnings{0};
    std::atomic<size_t> debug_count{0};
    std::atomic<size_t> info_count{0};
    std::atomic<size_t> fatal_count{0};
    std::chrono::steady_clock::time_point start_time;

    atomic_stats() : start_time(std::chrono::steady_clock::now()) {}

    void print_report() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - start_time
        ).count();
        double rate = elapsed > 0 ? static_cast<double>(total.load()) / elapsed : 0;

        std::cout << "\n=== [Thread A] Statistics Report ===\n";
        std::cout << "Total entries: " << total.load() << "\n";
        std::cout << "Errors: " << errors.load() << "\n";
        std::cout << "Warnings: " << warnings.load() << "\n";
        std::cout << "Info: " << info_count.load() << "\n";
        std::cout << "Debug: " << debug_count.load() << "\n";
        std::cout << "Fatal: " << fatal_count.load() << "\n";
        std::cout << "Processing rate: " << std::fixed << std::setprecision(2) << rate << " entries/sec\n";
        std::cout << "========================\n\n";
    }

    void print_final_report() const {
        size_t total_entries = total.load();
        size_t error_count = errors.load();
        double error_rate = total_entries > 0 ? (static_cast<double>(error_count) / total_entries) * 100 : 0;
        
        std::cout << "=== [Thread A] FINAL REPORT ===\n";
        std::cout << "Total entries processed: " << total_entries << "\n";
        std::cout << "Error rate: " << std::fixed << std::setprecision(2) << error_rate << "%\n";
        std::cout << "===================\n\n";
    }
};

// Структура для хранения паттернов
struct PatternMatch {
    std::string pattern_name;
    std::string source;
    std::string message;
    int count;
};

// Структура для агрегации метрик по минутам
struct MinuteMetric {
    int minute;
    int count;
    std::chrono::system_clock::time_point timestamp;
};