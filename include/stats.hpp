#pragma once

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

struct atomic_stats {
    std::atomic<size_t> total{0};
    std::atomic<size_t> errors{0};
    std::atomic<size_t> warnings{0};
    std::atomic<size_t> debug_count{0};
    std::atomic<size_t> info_count{0};
    std::chrono::steady_clock::time_point start_time;

    atomic_stats() : start_time(std::chrono::steady_clock::now()) {}

    void print_report() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - start_time
        ).count();
        double rate = elapsed > 0 ? static_cast<double>(total.load()) / elapsed : 0;

        std::cout << "\n===Statistic Report===\n";
              std::cout << "Total: " << total.load() 
                  << ", Errors: " << errors.load() << "\n";
        std::cout << "=================\n\n";
    }

    void print_final_report() const {
        print_report();
        // std::cout << ...
    }
};