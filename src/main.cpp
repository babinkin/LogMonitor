#include <csignal>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <queue>
#include <regex>
#include <sstream>
#include <string>
#include <thread>

#include "include/anomaly_detector.hpp"
#include "include/blocking_queue.hpp"
#include "include/error_logger.hpp"
#include "include/log_entry.hpp"
#include "include/performance_stats.hpp"
#include "include/source_grouper.hpp"
#include "include/stats.hpp"
#include "include/threadpool.hpp"

// Макрос логирования с выводом ID потока
#define LOG(msg) do { \
    std::ostringstream oss; \
    oss << "[Thread " << std::this_thread::get_id() << "] " << msg; \
    std::cout << oss.str() << std::endl; \
} while(false)

// Макрос логирования ошибки в отдельный файл
#define LOG_ERROR(msg) do { \
    std::ostringstream oss; \
    oss << "[Thread " << std::this_thread::get_id() << "] " << msg; \
    std::cout << oss.str() << std::endl; \
    if (g_error_logger) { \
        g_error_logger->log_error(oss.str()); \
    } \
} while(false)

// Глобальные переменные
std::unique_ptr<ErrorLogger> g_error_logger;            // логирование ошибок обработки в отдельный файл
std::unique_ptr<blocking_queue<LogEntry>> g_queue;      // потокобезопасная логирующая очередь
std::unique_ptr<PerformanceMonitor> g_perf_monitor;     // замеры производительности
std::atomic<bool> g_running{true};                      // флаг работы
std::atomic<bool> g_paused{false};                      // флаг паузы
std::mutex g_command_mutex;                             // мьютекс для синхронизации команд pause/resume/stats/exit
std::condition_variable g_command_stats_cv;             // условная переменная для команды stats
std::atomic<size_t> g_error_threshold{100};             // порог ошибок для отправки алерта
std::atomic<size_t> g_error_count{0};                   // текущее количество ошибок
std::atomic<size_t> g_total_processed{0};               // общее количество обработанных записей
std::atomic<bool> g_cmd_thread_running{true};           // условная переменная для потока чтения команды
std::atomic<bool> g_force_flush{false};                 // принудительный сброс статистики

// [Задание 2] Обработчик сигналов для корректной остановки очереди
void signal_handler(int signal) {
    LOG("Received signal " << signal << ", initiating shutdown...");
    g_running = false;
    if (g_queue) {
        g_queue->shutdown();
    }
}

// [Доп.задание 7] Асинхронная отправка уведомления при превышении порога ошибок
void send_alert(const std::string& message) {
    std::async(std::launch::async, [message]() {
        LOG_ERROR("ALERT: " << message);
    });
}

// [Доп.задание 7] Проверка превышения порога ошибок 
void check_error_threshold(std::atomic<size_t>& error_count, size_t threshold) {
    if (error_count.load() >= threshold) {
        std::ostringstream oss;
        oss << "Error threshold exceeded! Current errors: " << error_count.load() 
            << " (threshold: " << threshold << ")";
        send_alert(oss.str()); // отправка алерта
        error_count = 0; // сброс счетчика после отправка алерта
    }
}

// [Доп.задание 6] Динамическое управление: pause/resume/stats/exit
void command_handler(atomic_stats& stats) {
    std::string command;
    while (g_running && g_cmd_thread_running) {
        if (!g_running || !g_cmd_thread_running) {
            if (g_queue) {
                g_queue->shutdown();
            }
            break;
        }

        // получаем команду из стандартного потока ввода
        std::getline(std::cin, command);
        
        if (command == "pause") {
            LOG("Pausing processing...");
            g_paused = true;
            if (g_queue) {
                g_queue->pause();
            }
        } 
        else if (command == "resume") {
            LOG("Resuming processing...");
            g_paused = false;
            if (g_queue) {
                g_queue->resume();
            }
        }
        else if (command == "stats") {
            LOG("Manual stats requested");
            if (g_paused) {
                g_force_flush = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            stats.print_report();
        }
        else if (command == "exit") {
            LOG("Exit command received");
            g_running = false;
            g_cmd_thread_running = false;
            if (g_queue) {
                g_queue->shutdown(); 
            }
            break;
        }
        else {
            LOG_ERROR("Unknown command: " << command << ". Available: pause, resume, stats, exit");
        }
    }
}

// [Задание 3] Поток A: подсчёт ошибок по уровням (ERROR, WARN, INFO)
void count_stats(blocking_queue<LogEntry>& queue, atomic_stats& stats, 
                 std::atomic<bool>& running, PerformanceMonitor& perf_monitor,
                 size_t thread_id) {
    // локальная статистика
    size_t local_total = 0;
    size_t local_errors = 0;
    auto last_print = std::chrono::steady_clock::now();
    
    LOG("Thread A #" << thread_id << " started");
    
    try {
        while ((running || !queue.is_shutdown() || queue.size() > 0) && g_running) {
            // пауза
            while (g_paused && running) {
                 // Принудительный сброс при запросе stats
                 if (g_force_flush) {
                    if (local_total > 0 || local_errors > 0) {
                        stats.total += local_total;
                        stats.errors += local_errors;
                        g_total_processed += local_total;
                        local_total = 0;
                        local_errors = 0;
                    }
                    g_force_flush = false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            auto entry = queue.try_pop();

            // периодический вывод накопившейся статистики в консоль
            if (!entry.has_value()) {                
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_print);
                if (elapsed.count() >= 5 && (local_total > 0 || local_errors > 0)) {
                    stats.total += local_total;
                    stats.errors += local_errors;
                    g_total_processed += local_total;
                    local_total = 0;
                    local_errors = 0;
                    last_print = now;

                    // вывод отчета в реальном времени
                    std::lock_guard<std::mutex> lock(g_command_mutex);
                    std::cout << "\n5 seconds have passed! So, here are the statistics:\n";
                    stats.print_report();
                }
                if (queue.is_shutdown() && queue.size() == 0) {
                    break;  // выходим если очередь остановлена и пуста
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            local_total++;
            // увеличиваем счетчик операций, обработанных этим потоком 
            perf_monitor.record_processing(thread_id); 
            
            // определяем уровень ошибки
            switch (entry->level) {
                case LogLevel::ERROR:
                    local_errors++;
                    // асинхронная проверка превышения порога ошибок
                    g_error_count++;
                    check_error_threshold(g_error_count, g_error_threshold.load());
                    break;
                case LogLevel::DEBUG:
                    stats.debug_count++;
                    break;
                case LogLevel::INFO:
                    stats.info_count++;
                    break;
                case LogLevel::WARN:
                    stats.warnings++;
                    break;
                case LogLevel::FATAL:
                    stats.fatal_count++;
                    break;
                default:
                    break;
            }
        }
        
        // выврдим остаток локальной статистики
        stats.total += local_total;
        stats.errors += local_errors;
        g_total_processed += local_total;
        
    } catch (const std::exception& e) {
        if (g_error_logger) {
            g_error_logger->log_exception(e, "Thread A: " + std::to_string(thread_id));
        }
        LOG_ERROR("Exception in Thread A: " << thread_id << ": " << e.what());
    }
    
    LOG("Thread A #" << thread_id << " finished");
}

// [Задание 3] Поток B: выявление паттернов (регулярные выражения)
void find_patterns(blocking_queue<LogEntry>& queue, std::map<std::string, 
                   int>& pattern_counts, std::mutex& pattern_mutex, 
                   std::atomic<bool>& running, PerformanceMonitor& perf_monitor, size_t thread_id) {
    LOG("Thread B #" << thread_id << " started");
    
    try {
        // паттерны для выявления
        std::regex timeout_pattern(R"(timeout after (\d+)s)", std::regex::icase);
        std::regex cache_pattern(R"(cache (miss|hit))", std::regex::icase);
        std::regex connection_pattern(R"(connection (pool exhausted|established))", std::regex::icase);
        std::regex error_pattern(R"((error|failed|exception))", std::regex::icase);
        
        while ((running || !queue.is_shutdown() || queue.size() > 0) && g_running) {
            // пауза
            while (g_paused && running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            auto entry = queue.try_pop();
            if (!entry.has_value()) {
                if (queue.is_shutdown() && queue.size() == 0) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            // увеличиваем счетчик операций, обработанных этим потоком 
            perf_monitor.record_processing(thread_id); 
            
            std::smatch match;
            std::string full_msg = entry->source + " " + entry->message;
            
            std::lock_guard<std::mutex> lock(pattern_mutex);
            
            if (std::regex_search(full_msg, match, timeout_pattern)) {
                pattern_counts["timeout"]++;
            }
            else if (std::regex_search(full_msg, match, cache_pattern)) {
                pattern_counts["cache_" + match[1].str()]++;
            }
            else if (std::regex_search(full_msg, match, connection_pattern)) {
                pattern_counts["connection_" + match[1].str()]++;
            }
             else if (std::regex_search(full_msg, match, error_pattern)) {
                pattern_counts["error_keyword"]++;
            } else {
                pattern_counts["no_pattern"]++;
            }
        }
    } catch (const std::exception& e) {
        if (g_error_logger) {
            g_error_logger->log_exception(e, "Thread B: " + std::to_string(thread_id));
        }
        LOG_ERROR("Exception in Thread B: " << thread_id << ": " << e.what());
    }
    
    LOG("Thread B #" << thread_id << " finished");
}

// [Задание 3] Поток C: агрегация метрик (количество записей в минуту)
void aggregate_metrics(blocking_queue<LogEntry>& queue, std::map<int, int>& minute_counts,
                        std::mutex& metrics_mutex, std::atomic<bool>& running, PerformanceMonitor& perf_monitor, size_t thread_id) {
    LOG("Thread C #" << thread_id << " started");
    
    // локальный буфер для метрик
    std::map<int, int> local_minute_counts;
    auto last_print = std::chrono::steady_clock::now();
    
    try {
        while ((running || !queue.is_shutdown() || queue.size() > 0) && g_running) {
            // пауза
            while (g_paused && running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            auto entry = queue.try_pop();
            if (!entry.has_value()) {
                // периодический сброс накопленных метрик
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_print);
                if (elapsed.count() >= 10 && !local_minute_counts.empty()) {
                    std::lock_guard<std::mutex> lock(metrics_mutex);
                    for (const auto& [minute, count] : local_minute_counts) {
                        minute_counts[minute] += count;
                    }
                    local_minute_counts.clear();
                    last_print = now;
                }

                if (queue.is_shutdown() && queue.size() == 0) {
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            // увеличиваем счетчик операций, обработанных этим потоком 
            perf_monitor.record_processing(thread_id); 
            
            std::time_t time = std::chrono::system_clock::to_time_t(entry->timestamp);
            std::tm* tm = std::gmtime(&time);
            int minute_key = tm->tm_hour * 60 + tm->tm_min;
            if (minute_key >= 0 && minute_key < 1440) {
                local_minute_counts[minute_key]++;
            } else {
                LOG_ERROR("Invalid time: hour=" << tm->tm_hour << " min=" << tm->tm_min);
            }
        }
        
        // выводим остаток
        if (!local_minute_counts.empty()) {
            std::lock_guard<std::mutex> lock(metrics_mutex);
            for (const auto& [minute, count] : local_minute_counts) {
                if (minute >= 0 && minute < 1440) {
                    minute_counts[minute] += count;
                }
            }
        }
        
    } catch (const std::exception& e) {
        if (g_error_logger) {
            g_error_logger->log_exception(e, "Thread C: " + std::to_string(thread_id));
        }
        LOG_ERROR("Exception in Thread C: " << thread_id << ": " << e.what());
    }
    
    LOG("Thread C #" << thread_id << " finished");
}

// [Доп.задание 9] Поток группировки по источникам (SOURCE)
void grouping_by_source(blocking_queue<LogEntry>& queue, std::atomic<bool>& running, PerformanceMonitor& perf_monitor, 
                        SourceGrouper& source_grouper, size_t thread_id) {
    LOG("Thread D #" << thread_id << " started");
    while ((running || !queue.is_shutdown() || queue.size() > 0) && g_running) {
        while (g_paused && running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        auto entry = queue.try_pop();
        if (entry.has_value()) {
            source_grouper.add_entry(entry->source, entry->level);
            perf_monitor.record_processing(3); 
        } else if (queue.is_shutdown() && queue.size() == 0) {
            break;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    LOG("Thread D #" << thread_id << " finished");
}

// [Доп.задание 8] Производительность
void run_benchmark(const std::string& filename) {
    LOG("Running performance benchmark...");

    // Подсчет общего количества строк
    std::ifstream count_file(filename);
    size_t total_lines = 0;
    std::string line;
    while (std::getline(count_file, line)) {
        total_lines++;
    }
    count_file.close();
    
    // Однопоточная версия
    auto start_single = std::chrono::steady_clock::now();
    std::ifstream file_single(filename);
    size_t count_single = 0;
    while (std::getline(file_single, line)) {
        if (auto entry = LogEntry::parse(line)) {
            count_single++;
        }
    }
    auto end_single = std::chrono::steady_clock::now();
    auto duration_single = std::chrono::duration_cast<std::chrono::milliseconds>(end_single - start_single);
    
    // Многопоточная версия
    auto start_multi = std::chrono::steady_clock::now();
    blocking_queue<LogEntry> bench_queue(1000);
    std::atomic<size_t> count_multi{0};
    std::atomic<bool> bench_running{true};
    
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&]() {
            while (bench_running) {
                auto entry = bench_queue.try_pop();
                if (entry.has_value()) {
                    count_multi++;
                } else if (bench_queue.is_shutdown()) {
                    break;
                }
                std::this_thread::yield();
            }
        });
    }
    
    std::ifstream file_multi(filename);
    while (std::getline(file_multi, line)) {
        if (auto entry = LogEntry::parse(line)) {
            bench_queue.push(std::move(*entry));
        }
    }
    bench_queue.shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    bench_running = false;
    
    for (auto& w : workers) {
        w.join();
    }
    
    auto end_multi = std::chrono::steady_clock::now();
    auto duration_multi = std::chrono::duration_cast<std::chrono::milliseconds>(end_multi - start_multi);
    
    // Вывод результатов
    std::cout << "\n=== BENCHMARK RESULTS ===\n";
    std::cout << "Total entries: " << total_lines << "\n";
    std::cout << "Single-threaded: " << duration_single.count() << " ms (" << count_single << " entries)\n";
    std::cout << "Multi-threaded (4 threads): " << duration_multi.count() << " ms (" << count_multi << " entries)\n";
    double speedup = static_cast<double>(duration_single.count()) / duration_multi.count();
    std::cout << "Speedup: " << std::fixed << std::setprecision(2) << speedup << "x\n";
    
    // График ускорения (speedup) в зависимости от числа потоков
    std::cout << "\n=== Speedup ===\n";
    std::cout << "Threads | Throughput (entries/sec) | Speedup\n";
    std::cout << "--------|------------------------|---------\n";
    
    for (int threads = 1; threads <= 8; threads *= 2) {
        blocking_queue<LogEntry> scale_queue(1000);
        std::atomic<size_t> scale_count{0};
        std::atomic<bool> scale_running{true};
        
        std::vector<std::thread> scale_workers;
        auto scale_start = std::chrono::steady_clock::now();
        
        for (int t = 0; t < threads; ++t) {
            scale_workers.emplace_back([&]() {
                while (scale_running) {
                    auto entry = scale_queue.try_pop();
                    if (entry.has_value()) {
                        scale_count++;
                    } else if (scale_queue.is_shutdown()) {
                        break;
                    }
                    std::this_thread::yield();
                }
            });
        }
        
        std::ifstream scale_file(filename);
        while (std::getline(scale_file, line)) {
            if (auto entry = LogEntry::parse(line)) {
                scale_queue.push(std::move(*entry));
            }
        }
        scale_queue.shutdown();
        scale_running = false;
        
        for (auto& w : scale_workers) {
            w.join();
        }
        
        auto scale_end = std::chrono::steady_clock::now();
        auto scale_duration = std::chrono::duration_cast<std::chrono::milliseconds>(scale_end - scale_start);
        double throughput = (scale_count.load() * 1000.0) / scale_duration.count();
        double speedup_vs_single = (threads == 1) ? 1 : (throughput / (count_single * 1000.0 / duration_single.count()));

        if (threads == 1) {
            count_single = scale_count;
            duration_single = scale_duration;
        }
        
        std::cout << std::setw(6) << threads << " | " 
                  << std::setw(22) << std::fixed << std::setprecision(0) << throughput << " | "
                  << std::setw(7) << std::fixed << std::setprecision(2) << speedup_vs_single << "\n";
    }
    std::cout << "=========================\n\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: LogMonitor <logfile>\n";
        return 1;
    }

    std::cout << "==== LogMonitor Started ====\n";
    std::cout << "Available commands: pause, resume, stats, exit\n\n";
    
    // TODO: Установка обработчика сигналов
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Инициализация
    g_error_logger = std::make_unique<ErrorLogger>("error_log.txt");
    g_queue = std::make_unique<blocking_queue<LogEntry>>(10000); // Увеличенный размер
    
    LOG("LogMonitor initialized with queue size limit: 10000");
    
    try {
        // [Доп.задание 8] Замер производительности
        run_benchmark(argv[1]);
        
        // Инициализация компонентов
        size_t num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
        
        threadpool pool(num_threads + 2); // +2 для команд и мониторинга
        atomic_stats stats;
        g_perf_monitor = std::make_unique<PerformanceMonitor>(num_threads);
        
        std::map<std::string, int> pattern_counts;
        std::map<int, int> minute_counts;
        std::mutex pattern_mutex;
        std::mutex metrics_mutex;
        
        SourceGrouper source_grouper;
        AnomalyDetector anomaly_detector;
        
        // [Задание 3] Запуск consumer потоков 
        std::vector<std::future<void>> consumers;
        
        // Поток A
        consumers.push_back(pool.enqueue([&]() {
            count_stats(*g_queue, stats, g_running, *g_perf_monitor, 0);
        }));
        
        // Поток B
        consumers.push_back(pool.enqueue([&]() {
            find_patterns(*g_queue, pattern_counts, pattern_mutex, g_running, *g_perf_monitor, 1);
        }));
        
        // Поток C
        consumers.push_back(pool.enqueue([&]() {
            aggregate_metrics(*g_queue, minute_counts, metrics_mutex, g_running, *g_perf_monitor, 2);
        }));
        
        // [Доп.задание 9] Поток группировки по источникам (SOURCE)
        consumers.push_back(pool.enqueue([&]() {
            grouping_by_source(*g_queue, g_running, *g_perf_monitor, source_grouper, 3);
        }));
     
        
        // [Доп.задание 6] Поток обработчик команд pause/resume/stats/exit
        std::thread cmd_thread(command_handler, std::ref(stats));
        
        // Producer: чтение файла
        std::thread reader([&, filename = argv[1]](){
            LOG("Reader thread started, reading file: " << filename);
            try {
                std::ifstream file(filename);
                if (!file.is_open()) {
                    std::cerr << "Error: Cannot open file " << filename << "\n";
                    g_queue->shutdown();
                    g_running = false;
                    return;
                }
                
                std::string line;
                size_t line_count = 0;
                size_t parse_errors = 0;
                
                while (std::getline(file, line) && g_running) {
                    if (auto entry = LogEntry::parse(line)) {
                        g_queue->push(std::move(*entry));
                        line_count++;
                        if(line_count % 10000 == 0) {
                            LOG("Read " << line_count << " lines...");
                        }
                    } else {
                        parse_errors++;
                        if (g_error_logger) {
                            g_error_logger->log_error("Failed to parse line: " + line);
                        }
                    }
                }
                
                LOG("Finished reading. Total lines read: " << line_count 
                    << ", Parse errors: " << parse_errors);
                // Даем время потребителям обработать оставшиеся данные
                std::this_thread::sleep_for(std::chrono::seconds(2));
                g_queue->shutdown();
                
            } catch (const std::exception& e) {
                if (g_error_logger) {
                    g_error_logger->log_exception(e, "reader thread");
                }
                g_queue->shutdown();
                g_running = false;
            }
        });
        
        // [Задание 4] Сбор и вывод результатов
        auto last_anomaly_check = std::chrono::steady_clock::now();
        double prev_error_rate = 0;
        double prev_throughput = 0;
        
        while (g_running && !g_queue->is_shutdown()) {
            if (g_queue->size() == 0 && !g_running) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Меньше задержка
            
            // Проверка на команду stats
            {
                std::unique_lock<std::mutex> lock(g_command_mutex);
                if (g_command_stats_cv.wait_for(lock, std::chrono::milliseconds(1), []{ return true; })) {
                }
            }
                    
            // [Задание 9] Выявление аномалий (резкий рост ошибок) каждые 5 секунд
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_anomaly_check);
            if (elapsed.count() >= 5 && !g_paused) {
                // ТЕКУЩАЯ статистика
                double current_error_rate = stats.total > 0 ? 
                    (static_cast<double>(stats.errors.load()) / stats.total.load()) * 100 : 0;
                double current_throughput = g_perf_monitor->get_throughput();
                
                // ДЕЛЬТА (изменение за последние 5 секунд)
                double delta_error_rate = current_error_rate - prev_error_rate;
                double delta_throughput = current_throughput - prev_throughput;
                
                // Используем дельту для обнаружения аномалий
                anomaly_detector.add_metrics(delta_error_rate, delta_throughput);
                anomaly_detector.print_anomalies();
                
                // Сохраняем для следующей итерации
                prev_error_rate = current_error_rate;
                prev_throughput = current_throughput;
                last_anomaly_check = now;
            }
        }
        
        // Graceful shutdown
        LOG("Initiating graceful shutdown...");
        g_running = false;
        g_cmd_thread_running = false;

        // Ожидаем завершения reader
        if (reader.joinable()) {
            reader.join();
        }

        // Даем время потребителям завершить работу
        LOG("Waiting for consumers to finish processing...");
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // Ожидаем завершения всех consumer потоков
        pool.terminate();
        g_queue->shutdown();

        // Завершаем поток команд
        if (cmd_thread.joinable()) {
            g_cmd_thread_running = false;
            cmd_thread.detach();
        }
        
        // Фиксируем время
        g_perf_monitor->finish();
        
        // Финальные отчеты
        std::cout << "\n=== FINAL REPORTS ===\n";
        stats.print_report();
        stats.print_final_report();
        // [Задание 3] Вывод паттернов
        {
            std::lock_guard<std::mutex> lock(pattern_mutex);
            if (!pattern_counts.empty()) {
                std::cout << "\n=== [Thread B] PATTERN ANALYSIS ===\n";
                std::cout << "Total processed by Thread B: " << g_perf_monitor->get_thread_stats(1) << "\n\n";
                for (const auto& [pattern, count] : pattern_counts) {
                    std::cout << pattern << ": " << count << "\n";
                }
                std::cout << "=======================\n";
            }
        }

        source_grouper.print_report();
                
        // [Задание 3] Вывод метрик по минутам 
    {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        if (!minute_counts.empty()) {
            std::cout << "\n=== [Thread C] PER-MINUTE METRICS ===\n";
            std::cout << "from 10:00 to 11:59\n";
            std::vector<std::pair<int, int>> sorted_minutes(minute_counts.begin(), minute_counts.end());
            std::sort(sorted_minutes.begin(), sorted_minutes.end());
            for (const auto& [minute, count] : sorted_minutes) {
                int hour = minute / 60;
                int min = minute % 60;
                // для краткости выводим все минуты с 10:00 до 11:59
                if (hour >= 10 && hour <= 11 && min >= 0 && min <= 59) {
                    std::cout << std::setfill('0') << std::setw(2) << hour << ":"
                            << std::setw(2) << min << " - " << count << " entries\n";
                }
            }
            std::cout << "==========================\n";
        }
    }
        
        // [Доп. задание 9] Экспорт отчетов  JSON/CSV
        source_grouper.export_to_csv("source_report.csv");
        anomaly_detector.export_to_json("anomalies.json");
        anomaly_detector.export_to_csv("anomalies.csv");
        g_perf_monitor->export_to_csv("performance_report.csv");

        anomaly_detector.print_anomalies();
        g_perf_monitor->print_report();
        LOG("Reports exported to CSV/JSON files");
        LOG("LogMonitor finished successfully");
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        if (g_error_logger) {
            g_error_logger->log_exception(e, "main");
        }
        return 1;
    }
    
    return 0;
}