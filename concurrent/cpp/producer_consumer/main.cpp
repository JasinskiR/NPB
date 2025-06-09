#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <chrono>
#include <string>
#include <memory>
#include <random>
#include <iomanip>
#include <functional>
#include <future>
#include <deque>
#include <algorithm>

class ConcurrencyMetrics {
private:
    std::chrono::steady_clock::time_point start_time;
    std::atomic<std::size_t> mutex_operations{0};
    std::atomic<std::uint64_t> mutex_lock_times{0};
    std::atomic<std::size_t> channel_operations{0};
    std::atomic<std::uint64_t> channel_latencies{0};
    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};

public:
    ConcurrencyMetrics() : start_time(std::chrono::steady_clock::now()) {}
    
    void record_mutex_operation(std::chrono::nanoseconds duration) {
        mutex_operations.fetch_add(1);
        mutex_lock_times.fetch_add(duration.count());
    }
    
    void record_channel_operation(std::chrono::nanoseconds duration) {
        channel_operations.fetch_add(1);
        channel_latencies.fetch_add(duration.count());
    }
    
    void increment_produced() { produced.fetch_add(1); }
    void increment_consumed() { consumed.fetch_add(1); }
    
    double get_elapsed_seconds() {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        return std::chrono::duration<double>(elapsed).count();
    }
    
    double get_mutex_ops_per_sec() {
        double elapsed = get_elapsed_seconds();
        return elapsed > 0 ? mutex_operations.load() / elapsed : 0;
    }
    
    double get_avg_mutex_time_us() {
        auto ops = mutex_operations.load();
        return ops > 0 ? mutex_lock_times.load() / double(ops) / 1000.0 : 0;
    }
    
    double get_efficiency() {
        auto prod = produced.load();
        return prod > 0 ? consumed.load() / double(prod) * 100.0 : 0;
    }
    
    std::size_t get_produced() { return produced.load(); }
    std::size_t get_consumed() { return consumed.load(); }
    
    void print_results(const std::string& test_name) {
        double elapsed = get_elapsed_seconds();
        auto produced_count = produced.load();
        auto consumed_count = consumed.load();
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "C++ BENCHMARK RESULTS: " << test_name << "\n";
        std::cout << std::string(60, '=') << "\n";
        
        std::cout << "EXECUTION:\n";
        std::cout << "  Total time: " << std::fixed << std::setprecision(3) << elapsed << " s\n";
        std::cout << "  Produced: " << produced_count << " (" << std::fixed << std::setprecision(2) << produced_count / elapsed << "/s)\n";
        std::cout << "  Consumed: " << consumed_count << " (" << std::fixed << std::setprecision(2) << consumed_count / elapsed << "/s)\n";
        std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << get_efficiency() << "%\n";
        
        auto mutex_ops = mutex_operations.load();
        if (mutex_ops > 0) {
            std::cout << "\nMUTEX PERFORMANCE:\n";
            std::cout << "  Operations: " << mutex_ops << " (" << std::fixed << std::setprecision(2) << get_mutex_ops_per_sec() << " ops/s)\n";
            std::cout << "  Avg lock time: " << std::fixed << std::setprecision(2) << get_avg_mutex_time_us() << " μs\n";
        }
        
        auto channel_ops = channel_operations.load();
        if (channel_ops > 0) {
            std::cout << "CHANNEL PERFORMANCE:\n";
            std::cout << "  Operations: " << channel_ops << " (" << std::fixed << std::setprecision(2) << channel_ops / elapsed << " ops/s)\n";
            std::cout << "  Avg latency: " << std::fixed << std::setprecision(2) 
                      << channel_latencies.load() / double(channel_ops) / 1000.0 << " μs\n";
        }
    }
};

// Channel implementation using condition_variable (similar to Rust mpsc)
template<typename T>
class Channel {
private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool closed_ = false;

public:
    void send(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!closed_) {
            queue_.push(std::move(item));
            condition_.notify_one();
        }
    }

    bool try_recv(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            if (closed_) return false;
            return false; // Non-blocking, return immediately if empty
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool recv(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this]() { return !queue_.empty() || closed_; });
        
        if (queue_.empty() && closed_) return false;
        
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        condition_.notify_all();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
};

// ThreadSafeQueue implementation similar to Rust version
template<typename T>
class ThreadSafeQueue {
private:
    std::deque<T> queue_;
    mutable std::mutex mutex_;

public:
    void push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(item));
    }

    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
};

enum class ProducerConsumerMode {
    Channel,
    Queue
};

void producer_consumer_channel_benchmark(std::size_t num_producers, std::size_t num_consumers, std::size_t items_per_producer) {
    std::cout << "\nPRODUCER-CONSUMER CHANNEL BENCHMARK (C++)\n";
    std::cout << "Producers: " << num_producers << ", Consumers: " << num_consumers << ", Items per producer: " << items_per_producer << "\n";
    
    auto metrics = std::make_shared<ConcurrencyMetrics>();
    Channel<std::string> channel;
    std::atomic<bool> producers_done{false};
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    
    // Create producer threads
    for (std::size_t i = 0; i < num_producers; ++i) {
        producers.emplace_back([i, items_per_producer, &channel, metrics]() {
            for (std::size_t j = 0; j < items_per_producer; ++j) {
                auto start_send = std::chrono::steady_clock::now();
                std::string item = "Producer-" + std::to_string(i) + "-Item-" + std::to_string(j);
                channel.send(item);
                metrics->increment_produced();
                auto end_send = std::chrono::steady_clock::now();
                metrics->record_channel_operation(std::chrono::duration_cast<std::chrono::nanoseconds>(end_send - start_send));
                
                if (j % 100 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
            std::cout << "C++ Producer " << i << " finished\n";
        });
    }
    
    // Create consumer threads
    for (std::size_t i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([i, &channel, &producers_done, metrics]() {
            std::string item;
            std::size_t local_consumed = 0;
            
            while (true) {
                auto start_recv = std::chrono::steady_clock::now();
                bool received = channel.try_recv(item);
                
                if (received) {
                    auto end_recv = std::chrono::steady_clock::now();
                    metrics->record_channel_operation(std::chrono::duration_cast<std::chrono::nanoseconds>(end_recv - start_recv));
                    metrics->increment_consumed();
                    local_consumed++;
                } else {
                    if (producers_done.load() && channel.empty()) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }
            std::cout << "C++ Consumer " << i << " finished, consumed " << local_consumed << " items\n";
        });
    }
    
    // Wait for all producers to finish
    for (auto& t : producers) t.join();
    producers_done.store(true);
    
    // Give consumers time to finish remaining items
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Wait for all consumers to finish
    for (auto& t : consumers) t.join();
    
    metrics->print_results("Producer-Consumer Channel");
}

void producer_consumer_queue_benchmark(std::size_t num_producers, std::size_t num_consumers, std::size_t items_per_producer) {
    std::cout << "\nPRODUCER-CONSUMER QUEUE BENCHMARK (C++)\n";
    std::cout << "Producers: " << num_producers << ", Consumers: " << num_consumers << ", Items per producer: " << items_per_producer << "\n";
    
    auto metrics = std::make_shared<ConcurrencyMetrics>();
    ThreadSafeQueue<std::string> queue;
    std::atomic<bool> producers_done{false};
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    
    // Create producer threads
    for (std::size_t i = 0; i < num_producers; ++i) {
        producers.emplace_back([i, items_per_producer, &queue, metrics]() {
            for (std::size_t j = 0; j < items_per_producer; ++j) {
                auto start_send = std::chrono::steady_clock::now();
                std::string item = "Producer-" + std::to_string(i) + "-Item-" + std::to_string(j);
                queue.push(item);
                metrics->increment_produced();
                auto end_send = std::chrono::steady_clock::now();
                metrics->record_channel_operation(std::chrono::duration_cast<std::chrono::nanoseconds>(end_send - start_send));
                
                if (j % 100 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
            std::cout << "C++ Producer " << i << " finished\n";
        });
    }
    
    // Create consumer threads
    for (std::size_t i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([i, &queue, &producers_done, metrics]() {
            std::string item;
            std::size_t local_consumed = 0;
            
            while (true) {
                auto start_recv = std::chrono::steady_clock::now();
                bool received = queue.try_pop(item);
                
                if (received) {
                    auto end_recv = std::chrono::steady_clock::now();
                    metrics->record_channel_operation(std::chrono::duration_cast<std::chrono::nanoseconds>(end_recv - start_recv));
                    metrics->increment_consumed();
                    local_consumed++;
                } else {
                    if (producers_done.load() && queue.empty()) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }
            std::cout << "C++ Consumer " << i << " finished, consumed " << local_consumed << " items\n";
        });
    }
    
    // Wait for all producers to finish
    for (auto& t : producers) t.join();
    producers_done.store(true);
    
    // Give consumers time to finish remaining items
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Wait for all consumers to finish
    for (auto& t : consumers) t.join();
    
    metrics->print_results("Producer-Consumer Queue");
}

void producer_consumer_benchmark(ProducerConsumerMode mode, std::size_t num_producers, std::size_t num_consumers, std::size_t items_per_producer) {
    switch (mode) {
        case ProducerConsumerMode::Channel:
            producer_consumer_channel_benchmark(num_producers, num_consumers, items_per_producer);
            break;
        case ProducerConsumerMode::Queue:
            producer_consumer_queue_benchmark(num_producers, num_consumers, items_per_producer);
            break;
    }
}

void shared_data_mutex_benchmark(std::size_t num_threads, std::size_t operations_per_thread) {
    std::cout << "\nSHARED DATA MUTEX BENCHMARK (C++)\n";
    std::cout << "Threads: " << num_threads << ", Operations per thread: " << operations_per_thread << "\n";
    
    auto metrics = std::make_shared<ConcurrencyMetrics>();
    std::mutex shared_mutex;
    std::atomic<std::int64_t> shared_counter{0};
    std::vector<std::int32_t> shared_data;
    shared_data.reserve(num_threads * operations_per_thread);
    
    std::vector<std::thread> threads;
    
    for (std::size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back([i, operations_per_thread, &shared_mutex, &shared_counter, &shared_data, metrics]() {
            std::hash<std::size_t> hasher;
            
            for (std::size_t j = 0; j < operations_per_thread; ++j) {
                // First mutex operation - increment counter
                {
                    auto start = std::chrono::steady_clock::now();
                    std::lock_guard<std::mutex> lock(shared_mutex);
                    shared_counter.fetch_add(1);
                    auto end = std::chrono::steady_clock::now();
                    metrics->record_mutex_operation(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
                    metrics->increment_produced();
                }
                
                // Second mutex operation - add to vector
                {
                    auto start = std::chrono::steady_clock::now();
                    std::lock_guard<std::mutex> lock(shared_mutex);
                    std::size_t hash_input = i * 1000 + j;
                    shared_data.push_back(static_cast<std::int32_t>(hasher(hash_input)));
                    auto end = std::chrono::steady_clock::now();
                    metrics->record_mutex_operation(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
                    metrics->increment_consumed();
                }
                
                if (j % 50 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
            std::cout << "C++ Mutex thread " << i << " finished\n";
        });
    }
    
    for (auto& t : threads) t.join();
    
    auto final_counter = shared_counter.load();
    auto final_vec_size = shared_data.size();
    double peak_memory_mb = final_vec_size * sizeof(std::int32_t) / (1024.0 * 1024.0);
    
    std::cout << "\nMUTEX BENCHMARK RESULTS:\n";
    std::cout << "  Final counter value: " << final_counter << "\n";
    std::cout << "  Final vector size: " << final_vec_size << "\n";
    std::cout << "  Peak memory: " << std::fixed << std::setprecision(2) << peak_memory_mb << " MB\n";
    
    metrics->print_results("Shared Data Mutex");
}

void benchmark_csv_output(std::size_t max_threads, std::size_t items_per_test) {
    std::cout << "\nCSV OUTPUT FOR ANALYSIS:\n";
    std::cout << "Threads,Execution_Time_Sec,Mutex_Ops_Per_Sec,Avg_Mutex_Time_Us,Peak_Memory_MB,Efficiency_Percent\n";
    
    for (std::size_t threads = 1; threads <= max_threads; ++threads) {
        auto metrics = std::make_shared<ConcurrencyMetrics>();
        std::mutex shared_mutex;
        std::atomic<std::int64_t> shared_counter{0};
        std::vector<std::int32_t> shared_data;
        shared_data.reserve(threads * items_per_test);
        
        std::vector<std::thread> test_threads;
        
        for (std::size_t i = 0; i < threads; ++i) {
            test_threads.emplace_back([i, items_per_test, &shared_mutex, &shared_counter, &shared_data, metrics]() {
                std::hash<std::size_t> hasher;
                
                for (std::size_t j = 0; j < items_per_test; ++j) {
                    {
                        auto start = std::chrono::steady_clock::now();
                        std::lock_guard<std::mutex> lock(shared_mutex);
                        shared_counter.fetch_add(1);
                        auto end = std::chrono::steady_clock::now();
                        metrics->record_mutex_operation(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
                        metrics->increment_produced();
                    }
                    
                    {
                        auto start = std::chrono::steady_clock::now();
                        std::lock_guard<std::mutex> lock(shared_mutex);
                        std::size_t hash_input = i * 1000 + j;
                        shared_data.push_back(static_cast<std::int32_t>(hasher(hash_input)));
                        auto end = std::chrono::steady_clock::now();
                        metrics->record_mutex_operation(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
                        metrics->increment_consumed();
                    }
                    
                    if (j % 50 == 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(1));
                    }
                }
            });
        }
        
        for (auto& t : test_threads) t.join();
        
        double peak_memory_mb = shared_data.size() * sizeof(std::int32_t) / (1024.0 * 1024.0);
        
        std::cout << threads << ","
                  << std::fixed << std::setprecision(3) << metrics->get_elapsed_seconds() << ","
                  << std::fixed << std::setprecision(2) << metrics->get_mutex_ops_per_sec() << ","
                  << std::fixed << std::setprecision(2) << metrics->get_avg_mutex_time_us() << ","
                  << std::fixed << std::setprecision(1) << peak_memory_mb << ","
                  << std::fixed << std::setprecision(1) << 100.0 << "\n";
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void producer_consumer_ratio_test(ProducerConsumerMode mode, std::size_t total_threads, std::size_t items_per_producer) {
    std::cout << "\nPRODUCER-CONSUMER RATIO TEST\n";
    std::cout << "Testing different producer-consumer ratios with " 
              << (mode == ProducerConsumerMode::Channel ? "Channel" : "Queue") << " mode\n";
    std::cout << "Total threads: " << total_threads << ", Items per producer: " << items_per_producer << "\n";
    std::cout << "\nProducers,Consumers,Total_Time_Sec,Messages_Per_Sec,Efficiency_Percent\n";
    
    // Test different ratios where producers + consumers = total_threads
    std::vector<int> percentages = {10, 20, 30, 40, 50, 60, 70, 80, 90};
    for (int producer_pct : percentages) {
        std::size_t num_producers = std::max(total_threads * producer_pct / 100, std::size_t{1});
        std::size_t num_consumers = std::max(total_threads - num_producers, std::size_t{1});
        
        // For very small thread counts, avoid 0 producers or consumers
        if (num_producers == 0 || num_consumers == 0) {
            continue;
        }
        
        auto metrics = std::make_shared<ConcurrencyMetrics>();
        
        if (mode == ProducerConsumerMode::Channel) {
            Channel<std::string> channel;
            std::atomic<bool> producers_done{false};
            
            std::vector<std::thread> producers;
            std::vector<std::thread> consumers;
            
            // Spawn producers
            for (std::size_t i = 0; i < num_producers; ++i) {
                producers.emplace_back([i, items_per_producer, &channel, metrics]() {
                    for (std::size_t j = 0; j < items_per_producer; ++j) {
                        auto start_send = std::chrono::steady_clock::now();
                        std::string item = "Producer-" + std::to_string(i) + "-Item-" + std::to_string(j);
                        channel.send(item);
                        metrics->increment_produced();
                        auto end_send = std::chrono::steady_clock::now();
                        metrics->record_channel_operation(std::chrono::duration_cast<std::chrono::nanoseconds>(end_send - start_send));
                        
                        if (j % 100 == 0) {
                            std::this_thread::sleep_for(std::chrono::microseconds(1));
                        }
                    }
                });
            }
            
            // Spawn consumers
            for (std::size_t i = 0; i < num_consumers; ++i) {
                consumers.emplace_back([&channel, &producers_done, metrics]() {
                    std::string item;
                    while (true) {
                        auto start_recv = std::chrono::steady_clock::now();
                        bool received = channel.try_recv(item);
                        
                        if (received) {
                            auto end_recv = std::chrono::steady_clock::now();
                            metrics->record_channel_operation(std::chrono::duration_cast<std::chrono::nanoseconds>(end_recv - start_recv));
                            metrics->increment_consumed();
                        } else {
                            if (producers_done.load() && channel.empty()) {
                                break;
                            }
                            std::this_thread::sleep_for(std::chrono::microseconds(10));
                        }
                    }
                });
            }
            
            for (auto& t : producers) t.join();
            producers_done.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            for (auto& t : consumers) t.join();
            
        } else { // Queue mode
            ThreadSafeQueue<std::string> queue;
            std::atomic<bool> producers_done{false};
            
            std::vector<std::thread> producers;
            std::vector<std::thread> consumers;
            
            // Spawn producers
            for (std::size_t i = 0; i < num_producers; ++i) {
                producers.emplace_back([i, items_per_producer, &queue, metrics]() {
                    for (std::size_t j = 0; j < items_per_producer; ++j) {
                        auto start_send = std::chrono::steady_clock::now();
                        std::string item = "Producer-" + std::to_string(i) + "-Item-" + std::to_string(j);
                        queue.push(item);
                        metrics->increment_produced();
                        auto end_send = std::chrono::steady_clock::now();
                        metrics->record_channel_operation(std::chrono::duration_cast<std::chrono::nanoseconds>(end_send - start_send));
                        
                        if (j % 100 == 0) {
                            std::this_thread::sleep_for(std::chrono::microseconds(1));
                        }
                    }
                });
            }
            
            // Spawn consumers
            for (std::size_t i = 0; i < num_consumers; ++i) {
                consumers.emplace_back([&queue, &producers_done, metrics]() {
                    std::string item;
                    while (true) {
                        auto start_recv = std::chrono::steady_clock::now();
                        bool received = queue.try_pop(item);
                        
                        if (received) {
                            auto end_recv = std::chrono::steady_clock::now();
                            metrics->record_channel_operation(std::chrono::duration_cast<std::chrono::nanoseconds>(end_recv - start_recv));
                            metrics->increment_consumed();
                        } else {
                            if (producers_done.load() && queue.empty()) {
                                break;
                            }
                            std::this_thread::sleep_for(std::chrono::microseconds(10));
                        }
                    }
                });
            }
            
            for (auto& t : producers) t.join();
            producers_done.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            for (auto& t : consumers) t.join();
        }
        
        double elapsed = metrics->get_elapsed_seconds();
        std::size_t consumed = metrics->get_consumed();
        double msgs_per_sec = consumed / elapsed;
        double efficiency = metrics->get_efficiency();
        
        std::cout << num_producers << "," << num_consumers << "," 
                  << std::fixed << std::setprecision(3) << elapsed << ","
                  << std::fixed << std::setprecision(2) << msgs_per_sec << ","
                  << std::fixed << std::setprecision(1) << efficiency << "\n";
        
        // Brief pause between tests
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

struct BenchmarkConfig {
    std::size_t max_threads;
    std::size_t items_per_test;
    std::size_t csv_threads;
    std::size_t csv_items;
    bool run_producer_consumer;
    bool run_mutex_benchmark;
    bool run_csv_output;
    bool run_producer_consumer_ratio_test;
    ProducerConsumerMode producer_consumer_mode;
    bool help;
    
    BenchmarkConfig() 
        : max_threads(std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 4)
        , items_per_test(10000)
        , csv_threads(8)
        , csv_items(1000)
        , run_producer_consumer(true)
        , run_mutex_benchmark(true)
        , run_csv_output(true)
        , run_producer_consumer_ratio_test(false)
        , producer_consumer_mode(ProducerConsumerMode::Channel)
        , help(false) {}
};

BenchmarkConfig parse_args(int argc, char* argv[]) {
    BenchmarkConfig config;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            config.help = true;
            return config;
        } else if (arg == "--threads" || arg == "-t") {
            if (i + 1 < argc) {
                try {
                    std::size_t n = std::stoull(argv[++i]);
                    if (n > 0 && n <= 64) {
                        config.max_threads = n;
                    } else {
                        std::cerr << "Warning: Thread count " << n << " out of range (1-64), using default\n";
                    }
                } catch (...) {
                    std::cerr << "Warning: Invalid thread count '" << argv[i] << "', using default\n";
                }
            }
        } else if (arg == "--items" || arg == "-i") {
            if (i + 1 < argc) {
                try {
                    std::size_t n = std::stoull(argv[++i]);
                    if (n > 0 && n <= 1000000) {
                        config.items_per_test = n;
                    } else {
                        std::cerr << "Warning: Items count " << n << " out of range (1-1000000), using default\n";
                    }
                } catch (...) {
                    std::cerr << "Warning: Invalid items count '" << argv[i] << "', using default\n";
                }
            }
        } else if (arg == "--csv-threads") {
            if (i + 1 < argc) {
                try {
                    std::size_t n = std::stoull(argv[++i]);
                    if (n > 0 && n <= 32) {
                        config.csv_threads = n;
                    } else {
                        std::cerr << "Warning: CSV threads " << n << " out of range (1-32), using default\n";
                    }
                } catch (...) {
                    std::cerr << "Warning: Invalid CSV threads '" << argv[i] << "', using default\n";
                }
            }
        } else if (arg == "--csv-items") {
            if (i + 1 < argc) {
                try {
                    std::size_t n = std::stoull(argv[++i]);
                    if (n > 0 && n <= 100000) {
                        config.csv_items = n;
                    } else {
                        std::cerr << "Warning: CSV items " << n << " out of range (1-100000), using default\n";
                    }
                } catch (...) {
                    std::cerr << "Warning: Invalid CSV items '" << argv[i] << "', using default\n";
                }
            }
        } else if (arg == "--no-producer-consumer") {
            config.run_producer_consumer = false;
        } else if (arg == "--no-mutex") {
            config.run_mutex_benchmark = false;
        } else if (arg == "--no-csv") {
            config.run_csv_output = false;
        } else if (arg == "--mode" || arg == "-m") {
            if (i + 1 < argc) {
                std::string mode_str = argv[++i];
                if (mode_str == "channel") {
                    config.producer_consumer_mode = ProducerConsumerMode::Channel;
                } else if (mode_str == "queue") {
                    config.producer_consumer_mode = ProducerConsumerMode::Queue;
                } else {
                    std::cerr << "Warning: Invalid mode '" << mode_str << "', using channel\n";
                }
            }
        } else if (arg == "--ratio-test") {
            config.run_producer_consumer_ratio_test = true;
        } else {
            // Try to parse as positional arguments
            try {
                std::size_t n = std::stoull(arg);
                if (i == 1) { // First positional argument - max_threads
                    if (n > 0 && n <= 64) {
                        config.max_threads = n;
                    }
                } else if (i == 2) { // Second positional argument - items_per_test
                    if (n > 0 && n <= 1000000) {
                        config.items_per_test = n;
                    }
                }
            } catch (...) {
                std::cerr << "Warning: Unknown argument '" << arg << "'\n";
            }
        }
    }
    
    return config;
}

void print_usage(const char* program_name) {
    std::cout << "USAGE:\n";
    std::cout << "  " << program_name << " [OPTIONS] [max_threads] [items_per_test]\n\n";
    std::cout << "OPTIONS:\n";
    std::cout << "  -h, --help                 Show this help message\n";
    std::cout << "  -t, --threads <N>          Maximum number of threads (1-64, default: auto-detect)\n";
    std::cout << "  -i, --items <N>            Number of items per test (1-1000000, default: 10000)\n";
    std::cout << "  -m, --mode <MODE>          Producer-consumer mode: channel|queue (default: channel)\n";
    std::cout << "  --csv-threads <N>          Threads for CSV output (1-32, default: 8)\n";
    std::cout << "  --csv-items <N>            Items for CSV output (1-100000, default: 1000)\n";
    std::cout << "  --no-producer-consumer     Skip producer-consumer benchmark\n";
    std::cout << "  --no-mutex                 Skip mutex benchmark\n";
    std::cout << "  --no-csv                   Skip CSV output\n";
    std::cout << "  --ratio-test              Test different producer-consumer ratios\n\n";
    std::cout << "EXAMPLES:\n";
    std::cout << "  " << program_name << "                         # Default settings\n";
    std::cout << "  " << program_name << " --threads 4 --items 5000     # 4 threads, 5000 items\n";
    std::cout << "  " << program_name << " -t 8 -i 10000 -m queue       # 8 threads, queue mode\n";
    std::cout << "  " << program_name << " --no-csv                      # Skip CSV output\n";
}

std::string get_os_info() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

std::string get_cpu_architecture() {
#if defined(__APPLE__) && defined(__aarch64__)
    return "Apple Silicon";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__aarch64__)
    return "aarch64";
#elif defined(__arm__)
    return "arm";
#elif defined(__i386__)
    return "x86 (32-bit)";
#else
    return "unknown";
#endif
}

int main(int argc, char* argv[]) {
    auto config = parse_args(argc, argv);
    
    if (config.help) {
        print_usage(argv[0]);
        return 0;
    }
    
    auto system_cores = std::thread::hardware_concurrency();
    
    std::cout << std::string(80, '=') << "\n";
    std::cout << "C++ CONCURRENCY MECHANISMS COMPREHENSIVE BENCHMARK\n";
    std::cout << std::string(80, '=') << "\n";
    
    std::cout << "PLATFORM:\n";
    std::cout << "  System: " << get_os_info() << "\n";
    std::cout << "  Architecture: " << get_cpu_architecture() << "\n";
    std::cout << "  Available cores: " << system_cores << "\n";
    
    std::cout << "\nCONFIGURATION:\n";
    std::cout << "  Max threads used: " << config.max_threads;
    if (config.max_threads > system_cores) {
        std::cout << " (exceeds physical cores)";
    }
    std::cout << "\n";
    std::cout << "  Items per test: " << config.items_per_test << "\n";
    std::cout << "  Producer-consumer mode: " 
              << (config.producer_consumer_mode == ProducerConsumerMode::Channel ? "Channel" : "Queue") << "\n";
    std::cout << "  Concurrency scaling: " << std::fixed << std::setprecision(1) 
              << config.max_threads / double(system_cores) << "x logical cores\n";
    std::cout << "  Profile: " << 
#ifdef NDEBUG
        "release" 
#else
        "debug"
#endif
        << "\n";

#ifndef NDEBUG
    std::cout << "\nWARNING: Running in DEBUG mode! Use -O3 -DNDEBUG for accurate benchmarks!\n";
#endif

    if (config.max_threads > system_cores * 2) {
        std::cout << "\nWARNING: Using " << config.max_threads / system_cores 
                  << "x more threads than cores may cause performance degradation\n";
    }

    auto threads_per_test = std::min(config.max_threads, std::size_t{8});
    auto producers_consumers = std::max(threads_per_test / 2, std::size_t{1});

    std::cout << "\nTEST SCENARIOS:\n";
    if (config.run_producer_consumer) {
        std::cout << "  Producer-Consumer: " << producers_consumers << " producers, " << producers_consumers << " consumers\n";
    }
    if (config.run_mutex_benchmark) {
        std::cout << "  Mutex contention: " << threads_per_test << " threads\n";
    }
    if (config.run_csv_output) {
        std::cout << "  CSV analysis: 1-" << config.csv_threads << " threads, " << config.csv_items << " items each\n";
    }

    if (config.run_producer_consumer) {
        producer_consumer_benchmark(config.producer_consumer_mode, producers_consumers, producers_consumers, config.items_per_test);
    }
    
    if (config.run_mutex_benchmark) {
        shared_data_mutex_benchmark(threads_per_test, config.items_per_test);
    }
    
    if (config.run_csv_output) {
        benchmark_csv_output(config.csv_threads, config.csv_items);
    }
    
    if (config.run_producer_consumer && config.run_producer_consumer_ratio_test) {
        producer_consumer_ratio_test(
            config.producer_consumer_mode,
            std::min(config.max_threads, std::size_t{16}), // Limit threads for ratio test
            config.items_per_test / 2   // Use fewer items per producer for ratio test
        );
    }

    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "C++ BENCHMARK COMPLETED\n";
    std::cout << std::string(80, '=') << "\n";

    return 0;
}