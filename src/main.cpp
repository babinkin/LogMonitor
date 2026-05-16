#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

#include "include/blocking_queue.hpp"
#include "include/log_entry.hpp"
#include "include/stats.hpp"
#include "include/threadpool.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr<<"Usage: LogMonitor <logfile>\n";
        return 1;
    }

    std::cout << "====LogMonitor Started===\n\n";
    
    // 1. Инициализация компонентов
    blocking_queue<LogEntry> queue(1000); // очередь с лимитом
    threadpool pool(std::thread::hardware_concurrency());
    atomic_stats stats;

    auto future1 = pool.enqueue([&queue, &stats]() {
        for (;;) {
            auto entry = queue.try_pop();

            if (!entry.has_value()) {
                //...
                continue;
            }
            stats.total++;
            switch (entry->level) {
                case LogLevel::ERROR:
                    stats.errors++;
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
                default:
                    break;
            }
        }
    });
    
    /*
    // 2. Запуск анализаторов (consumers - рабочих потоков)
    auto error_counter = pool.async(count_errors, std::ref(queue), std::ref(stats));
    auto pattern_finder = pool.async(find_patterns, std::ref(queue), std::ref(stats));
    auto metrics_agg = pool.async(aggregate_metrics, std::ref(queue), std::ref(stats));
    */
    
    // 3. Producer: чтение файла
    std::thread reader([&queue, filename = argv[1]](){
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open file " << filename << "\n";
            queue.shutdown();
            return;
        }
        std::string line;
        size_t line_count = 0;

        while (std::getline(file, line)) {
            if (auto entry = LogEntry::parse(line)) {
                queue.push(std::move(*entry)); // Блокирует, если очередь полна
                line_count++;
                if(line_count % 1000 == 0) {
                    std::cout << "Read " << line_count << "lines... \n";
                }
            }
        }
        queue.shutdown(); // сигнал завершения
    });
    
    // 4. Мониторинг: вывод статистики
    while(!queue.is_shutdown() || queue.size() > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        stats.print_report(); // Атомарный вывод
    }
    
    // 5.Завершение
    reader.join();
    //future1.join();
    pool.terminate(); // ждём завершения всех задач
    
    stats.print_final_report();
    return 0;
}
