// performance_stats.hpp
#pragma once

#include <chrono>
#include <atomic>
#include <vector>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <thread>

// alignas для избежания false sharing
struct alignas(64) PaddedStats {
    std::atomic<size_t> counter{0};
    char padding[64 - sizeof(std::atomic<size_t>)];
};

class PerformanceMonitor {
private:
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::atomic<size_t> total_processed{0};
    std::vector<PaddedStats> thread_stats;
    
public:
    PerformanceMonitor(size_t num_threads) : thread_stats(num_threads) {
        start_time = std::chrono::steady_clock::now();
    }
    
    void record_processing(size_t thread_id) {
        thread_stats[thread_id].counter++;
        total_processed++;
    }

    size_t get_thread_stats(size_t thread_id) {
        return thread_stats[thread_id].counter.load();
    }
    
    void finish() {
        end_time = std::chrono::steady_clock::now();
    }
    
    double get_elapsed_seconds() const {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        return duration.count() / 1000000.0;
    }
    
    size_t get_total_processed() const {
        return total_processed.load();
    }
    
    double get_throughput() const {
        double elapsed = get_elapsed_seconds();
        return elapsed > 0 ? total_processed.load() / elapsed : 0;
    }
    
    void print_report() const {
        std::cout << "\n=== PERFORMANCE REPORT ===\n";
        std::cout << "Total processed: " << total_processed.load() << "\n";
        std::cout << "Elapsed time: " << std::fixed << std::setprecision(2) 
                  << get_elapsed_seconds() << " seconds\n";
        std::cout << "Throughput: " << std::fixed << std::setprecision(2) 
                  << get_throughput() << " entries/sec\n";
        
        // Статистика по потокам
        for (size_t i = 0; i < thread_stats.size(); ++i) {
            std::cout << "Thread " << i << " processed: " << thread_stats[i].counter.load() << "\n";
        }
        std::cout << "==========================\n\n";
    }
    
    void export_to_csv(const std::string& filename) const {
        std::ofstream file(filename);
        if (file.is_open()) {
            file << "Metric,Value\n";
            file << "Total Processed," << total_processed.load() << "\n";
            file << "Elapsed Time (s)," << get_elapsed_seconds() << "\n";
            file << "Throughput (entries/sec)," << get_throughput() << "\n";
            file.close();
        }
    }
};

// Thread-local статистика для избежания contention
class ThreadLocalStats {
private:
    struct LocalData {
        size_t processed = 0;
        size_t errors = 0;
        std::chrono::steady_clock::time_point last_flush;
    };
    
    thread_local static LocalData local_data;
    std::atomic<size_t>& global_processed;
    std::atomic<size_t>& global_errors;
    std::mutex& flush_mutex;
    
public:
    ThreadLocalStats(std::atomic<size_t>& global_processed, 
                     std::atomic<size_t>& global_errors,
                     std::mutex& flush_mutex)
        : global_processed(global_processed), global_errors(global_errors), flush_mutex(flush_mutex) {}
    
    void increment_processed() {
        local_data.processed++;
    }
    
    void increment_errors() {
        local_data.errors++;
    }
    
    void flush_if_needed() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - local_data.last_flush);
        
        if (elapsed.count() >= 5 && (local_data.processed > 0 || local_data.errors > 0)) {
            std::lock_guard<std::mutex> lock(flush_mutex);
            global_processed += local_data.processed;
            global_errors += local_data.errors;
            local_data.processed = 0;
            local_data.errors = 0;
            local_data.last_flush = now;
        }
    }
    
    ~ThreadLocalStats() {
        // Финализируем при разрушении
        std::lock_guard<std::mutex> lock(flush_mutex);
        global_processed += local_data.processed;
        global_errors += local_data.errors;
    }
};

// Определение thread_local статического члена
thread_local ThreadLocalStats::LocalData ThreadLocalStats::local_data{0, 0, std::chrono::steady_clock::now()};