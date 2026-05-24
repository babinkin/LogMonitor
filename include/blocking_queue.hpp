// blocking_queue.hpp - добавьте метод reserve
#pragma once

#include <condition_variable>
#include <limits>
#include <mutex>
#include <optional>
#include <type_traits>
#include <queue>
#include <utility>

template<typename T>
class blocking_queue {
private:
    std::queue<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    size_t max_size_;
    size_t current_size_;
    bool shutdown_; // флаг остановки
    bool paused_;   // флаг паузы (новый)

public:
    // конструктор с опциональным ограничением размера
    explicit blocking_queue(size_t max_size = std::numeric_limits<size_t>::max())
        : max_size_(max_size), shutdown_(false), paused_(false) {}

    ~blocking_queue() = default;

    // запрещаем копирование и перемещение
    blocking_queue(const blocking_queue&) = delete;
    blocking_queue(blocking_queue&&) = delete;
    blocking_queue& operator=(const blocking_queue&) = delete;
    blocking_queue& operator=(blocking_queue&&) = delete;

    // Резервирование памяти для queue_ (если queue поддерживает reserve)
    void reserve(size_t capacity) {
        // std::queue не имеет reserve, используем другой подход
        // Для std::queue мы не можем зарезервировать память, 
        // но можем использовать std::deque как контейнер по умолчанию
        // Оставляем как есть, т.к. queue не поддерживает reserve
    }

    // управление жизненным циклом
    void shutdown() noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        shutdown_ = true;
        // разбудить все ожидающие потоки (и потребителей и производителей)
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }
    
    // Управление паузой
    void pause() noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        paused_ = true;
    }
    
    void resume() noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        paused_ = false;
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }
    
    bool is_paused() const noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        return paused_;
    }

    // проверка флага остановки
    bool is_shutdown() const noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        return shutdown_;
    }

    // Блокирующие операции (основной интерфейс)
    // блокирующее добавление (ждет, если очередь полна)
    void push(const T& item) {
        std::unique_lock<std::mutex> lock(mtx_);
        // ждем: очередь полна или запрошена остановка или пауза
        cv_not_full_.wait(lock, [this] {
            return (queue_.size() < max_size_ && !paused_) || shutdown_;
        });
        // если остановка запрошена или очередь полна - не добавляем
        if (shutdown_ && queue_.size() >= max_size_) {
            return;
        }
        queue_.push(item);
        cv_not_empty_.notify_one();
    }

    // блокирующее извлечение (ждет, если очередь пуста)
    T pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        // ждем: очередь не пуста или запрошена остановка
        cv_not_empty_.wait(lock, [this] {
            return (!queue_.empty() && !paused_) || shutdown_;
        });

        // если остановка и очередь пуста - выбрасываем исключение
        if(shutdown_ && queue_.empty()) {
            throw std::runtime_error("Queue shutdown: no more items");
        }

        // для безопасности при исключениях используем move_if_noexcept
        T value = std::move_if_noexcept(queue_.front());
        queue_.pop();

        cv_not_full_.notify_one();
        return value;
    }

    // Атомарная проверка + извлечение: try_pop()
    // неблокирующее извлечение
    std::optional<T> try_pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        if(queue_.empty() || paused_) {
            return std::nullopt;
        }
        T value = std::move_if_noexcept(queue_.front());
        queue_.pop();

        cv_not_full_.notify_one();
        return value;
    }

    // Наблюдатели (использовать с осторожностью)
    size_t size() const noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size();
    }

    size_t max_size() const noexcept {
        return max_size_;
    }

    // Очистка очереди 
    void clear() noexcept {
        std::unique_lock<std::mutex> lock(mtx_);
        while (!queue_.empty()) {
            queue_.pop();
        }
        // уведомляем пользователей, что места освободились 
        cv_not_full_.notify_all();
    }
};