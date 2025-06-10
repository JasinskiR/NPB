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
#include <fstream>
#include <sstream>

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

struct MemoryStats {
    size_t rss_kb = 0;
    size_t peak_rss_kb = 0;
    size_t heap_kb = 0;
    size_t thread_overhead_kb = 0;
    size_t heap_size_estimated_kb = 0;
    size_t runtime_overhead_kb = 0;
    
    static MemoryStats current() {
        MemoryStats stats;
        
#ifdef __linux__
        std::ifstream status("/proc/self/status");
        std::string line;
        
        while (std::getline(status, line)) {
            if (line.substr(0, 6) == "VmRSS:") {
                std::istringstream iss(line);
                std::string label, value;
                iss >> label >> value;
                stats.rss_kb = std::stoull(value);
            } else if (line.substr(0, 6) == "VmHWM:") {
                std::istringstream iss(line);
                std::string label, value;
                iss >> label >> value;
                stats.peak_rss_kb = std::stoull(value);
            }
        }
        
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            stats.peak_rss_kb = std::max(stats.peak_rss_kb, static_cast<size_t>(usage.ru_maxrss));
        }
        
#elif defined(__APPLE__)
        struct mach_task_basic_info info;
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                     reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
            stats.rss_kb = info.resident_size / 1024;
        }
        
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            stats.peak_rss_kb = usage.ru_maxrss / 1024;
        }
        
#elif defined(_WIN32)
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), 
                               reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), 
                               sizeof(pmc))) {
            stats.rss_kb = pmc.WorkingSetSize / 1024;
            stats.peak_rss_kb = pmc.PeakWorkingSetSize / 1024;
            stats.heap_kb = pmc.PrivateUsage / 1024;
        }
#endif
        return stats;
    }
    
    void estimate_heap_size(const std::vector<std::pair<std::string, size_t>>& data_structures) {
        size_t total_estimated = 0;
        
        std::cout << "\nHEAP ANALYSIS:\n";
        for (const auto& [name, size_bytes] : data_structures) {
            size_t size_kb = size_bytes / 1024;
            total_estimated += size_kb;
            std::cout << "  Structure '" << name << "': " << size_kb << " KB\n";
        }
        
        heap_size_estimated_kb = total_estimated;
        std::cout << "  Total estimated heap: " << total_estimated << " KB\n";
    }
    
    void estimate_thread_overhead(size_t num_threads) {
        constexpr size_t THREAD_STACK_SIZE_KB = 2048;
        constexpr size_t THREAD_METADATA_KB = 8;
        constexpr size_t STD_THREAD_OVERHEAD_KB = 4;
        
        thread_overhead_kb = num_threads * (THREAD_STACK_SIZE_KB + THREAD_METADATA_KB + STD_THREAD_OVERHEAD_KB);
        
        std::cout << "\nTHREAD OVERHEAD ANALYSIS:\n";
        std::cout << "  Threads: " << num_threads << "\n";
        std::cout << "  Stack per thread: " << THREAD_STACK_SIZE_KB << " KB\n";
        std::cout << "  Metadata per thread: " << THREAD_METADATA_KB << " KB\n";
        std::cout << "  C++ std::thread overhead: " << STD_THREAD_OVERHEAD_KB << " KB\n";
        std::cout << "  Total thread overhead: " << thread_overhead_kb << " KB\n";
    }
    
    void calculate_runtime_overhead() {
        runtime_overhead_kb = rss_kb > (heap_size_estimated_kb + thread_overhead_kb) 
                             ? rss_kb - heap_size_estimated_kb - thread_overhead_kb 
                             : 0;
    }
    
    void print_comprehensive_analysis(const std::string& test_name, const MemoryStats& baseline) const {
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "COMPREHENSIVE MEMORY ANALYSIS: " << test_name << "\n";
        std::cout << std::string(70, '=') << "\n";
        
        std::cout << "\nBASIC MEMORY METRICS:\n";
        std::cout << "  Baseline RSS: " << baseline.rss_kb << " KB (" << baseline.rss_kb / 1024.0 << " MB)\n";
        std::cout << "  Current RSS: " << rss_kb << " KB (" << rss_kb / 1024.0 << " MB)\n";
        std::cout << "  Memory growth: " << (rss_kb > baseline.rss_kb ? rss_kb - baseline.rss_kb : 0) 
                  << " KB (" << (rss_kb > baseline.rss_kb ? (rss_kb - baseline.rss_kb) / 1024.0 : 0.0) << " MB)\n";
        std::cout << "  Peak RSS: " << peak_rss_kb << " KB (" << peak_rss_kb / 1024.0 << " MB)\n";
        
        std::cout << "\nMEMORY BREAKDOWN:\n";
        std::cout << "  Estimated heap: " << heap_size_estimated_kb << " KB (" << heap_size_estimated_kb / 1024.0 << " MB)\n";
        std::cout << "  Thread overhead: " << thread_overhead_kb << " KB (" << thread_overhead_kb / 1024.0 << " MB)\n";
        std::cout << "  Runtime overhead: " << runtime_overhead_kb << " KB (" << runtime_overhead_kb / 1024.0 << " MB)\n";
        
        std::cout << "\nMEMORY EFFICIENCY:\n";
        if (rss_kb > 0) {
            double heap_ratio = (heap_size_estimated_kb * 100.0) / rss_kb;
            double thread_ratio = (thread_overhead_kb * 100.0) / rss_kb;
            double runtime_ratio = (runtime_overhead_kb * 100.0) / rss_kb;
            
            std::cout << "  Heap efficiency: " << std::fixed << std::setprecision(1) << heap_ratio << "%\n";
            std::cout << "  Thread overhead: " << std::fixed << std::setprecision(1) << thread_ratio << "%\n";
            std::cout << "  Runtime overhead: " << std::fixed << std::setprecision(1) << runtime_ratio << "%\n";
        }
        
#ifdef _WIN32
        if (heap_kb > 0) {
            std::cout << "\nWINDOWS SPECIFIC:\n";
            std::cout << "  Private heap usage: " << heap_kb << " KB (" << heap_kb / 1024.0 << " MB)\n";
        }
#endif
    }
    
    double get_peak_mb() const {
        return peak_rss_kb / 1024.0;
    }
    
    double get_current_mb() const {
        return rss_kb / 1024.0;
    }
    
    size_t get_memory_growth_kb(const MemoryStats& baseline) const {
        return rss_kb > baseline.rss_kb ? rss_kb - baseline.rss_kb : 0;
    }
};

class EnhancedConcurrencyMetrics {
private:
    std::chrono::steady_clock::time_point start_time;
    std::atomic<std::size_t> mutex_operations{0};
    std::atomic<std::uint64_t> mutex_lock_times{0};
    std::atomic<std::size_t> channel_operations{0};
    std::atomic<std::uint64_t> channel_latencies{0};
    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    mutable std::mutex memory_mutex;
    MemoryStats baseline_memory;
    MemoryStats current_memory;

public:
    EnhancedConcurrencyMetrics() : start_time(std::chrono::steady_clock::now()) {
        baseline_memory = MemoryStats::current();
        current_memory = baseline_memory;
    }
    
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
    
    void update_memory_analysis(const std::vector<std::pair<std::string, size_t>>& data_structures, 
                               size_t num_threads) {
        std::lock_guard<std::mutex> lock(memory_mutex);
        
        current_memory = MemoryStats::current();
        current_memory.estimate_heap_size(data_structures);
        current_memory.estimate_thread_overhead(num_threads);
        current_memory.calculate_runtime_overhead();
    }
    
    double get_elapsed_seconds() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        return std::chrono::duration<double>(elapsed).count();
    }
    
    double get_mutex_ops_per_sec() const {
        double elapsed = get_elapsed_seconds();
        return elapsed > 0 ? mutex_operations.load() / elapsed : 0;
    }
    
    double get_avg_mutex_time_us() const {
        auto ops = mutex_operations.load();
        return ops > 0 ? mutex_lock_times.load() / double(ops) / 1000.0 : 0;
    }
    
    double get_efficiency() const {
        auto prod = produced.load();
        return prod > 0 ? consumed.load() / double(prod) * 100.0 : 0;
    }
    
    std::size_t get_produced() const { return produced.load(); }
    std::size_t get_consumed() const { return consumed.load(); }
    
    void print_comprehensive_results(const std::string& test_name) {
        std::lock_guard<std::mutex> lock(memory_mutex);
        
        // Update final memory stats
        current_memory = MemoryStats::current();
        current_memory.calculate_runtime_overhead();
        
        double elapsed = get_elapsed_seconds();
        auto produced_count = produced.load();
        auto consumed_count = consumed.load();
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "C++ ENHANCED BENCHMARK RESULTS: " << test_name << "\n";
        std::cout << std::string(60, '=') << "\n";
        
        std::cout << "\nEXECUTION PERFORMANCE:\n";
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
            std::cout << "\nCHANNEL PERFORMANCE:\n";
            std::cout << "  Operations: " << channel_ops << " (" << std::fixed << std::setprecision(2) << channel_ops / elapsed << " ops/s)\n";
            std::cout << "  Avg latency: " << std::fixed << std::setprecision(2) 
                      << channel_latencies.load() / double(channel_ops) / 1000.0 << " μs\n";
        }
        
        current_memory.print_comprehensive_analysis(test_name, baseline_memory);
    }
    
    double get_peak_memory_mb() const {
        std::lock_guard<std::mutex> lock(memory_mutex);
        return current_memory.get_peak_mb();
    }
    
    double get_current_memory_mb() const {
        std::lock_guard<std::mutex> lock(memory_mutex);
        return current_memory.get_current_mb();
    }
    
    size_t get_memory_growth_kb() const {
        std::lock_guard<std::mutex> lock(memory_mutex);
        return current_memory.get_memory_growth_kb(baseline_memory);
    }
};

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
            return false;
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
    
    size_t get_memory_usage() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sizeof(*this) + queue_.size() * sizeof(T);
    }
};

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
    
    size_t get_memory_usage() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sizeof(*this) + queue_.size() * sizeof(T);
    }
};

void enhanced_producer_consumer_channel_benchmark(std::size_t num_producers, std::size_t num_consumers, std::size_t items_per_producer) {
    std::cout << "\nENHANCED PRODUCER-CONSUMER CHANNEL BENCHMARK (C++)\n";
    std::cout << "Producers: " << num_producers << ", Consumers: " << num_consumers << ", Items per producer: " << items_per_producer << "\n";
    
    auto metrics = std::make_shared<EnhancedConcurrencyMetrics>();
    Channel<std::string> channel;
    std::atomic<bool> producers_done{false};
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    
    const size_t total_threads = num_producers + num_consumers;
    const size_t total_expected_items = num_producers * items_per_producer;
    
    std::vector<std::pair<std::string, size_t>> data_structures = {
        {"Channel<std::string>", channel.get_memory_usage()},
        {"std::atomic<bool>", sizeof(std::atomic<bool>)},
        {"String buffers (estimated)", total_expected_items * 32},
        {"std::vector<std::thread> producers", num_producers * sizeof(std::thread)},
        {"std::vector<std::thread> consumers", num_consumers * sizeof(std::thread)},
        {"EnhancedConcurrencyMetrics", sizeof(EnhancedConcurrencyMetrics)},
    };
    
    metrics->update_memory_analysis(data_structures, total_threads);
    
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
            std::cout << "C++ Enhanced Producer " << i << " finished\n";
        });
    }
    
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
            std::cout << "C++ Enhanced Consumer " << i << " finished, consumed " << local_consumed << " items\n";
        });
    }
    
    for (auto& t : producers) t.join();
    producers_done.store(true);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    for (auto& t : consumers) t.join();
    
    std::vector<std::pair<std::string, size_t>> final_data_structures = {
        {"Final channel state", channel.get_memory_usage()},
        {"Cleanup overhead", 1024},
    };
    metrics->update_memory_analysis(final_data_structures, 1);
    
    metrics->print_comprehensive_results("Enhanced Producer-Consumer Channel");
}

void enhanced_shared_data_mutex_benchmark(std::size_t num_threads, std::size_t operations_per_thread) {
    std::cout << "\nENHANCED SHARED DATA MUTEX BENCHMARK (C++)\n";
    std::cout << "Threads: " << num_threads << ", Operations per thread: " << operations_per_thread << "\n";
    
    auto metrics = std::make_shared<EnhancedConcurrencyMetrics>();
    std::mutex shared_mutex;
    std::atomic<std::int64_t> shared_counter{0};
    std::vector<std::int32_t> shared_data;
    shared_data.reserve(num_threads * operations_per_thread);
    
    // Remove or comment out this line to fix the warning
    // const size_t total_expected_items = num_threads * operations_per_thread;
    
    std::vector<std::pair<std::string, size_t>> data_structures = {
        {"std::mutex shared_mutex", sizeof(std::mutex)},
        {"std::atomic<int64_t>", sizeof(std::atomic<std::int64_t>)},
        {"std::vector<int32_t> capacity", shared_data.capacity() * sizeof(std::int32_t)},
        {"std::vector<std::thread>", num_threads * sizeof(std::thread)},
        {"EnhancedConcurrencyMetrics", sizeof(EnhancedConcurrencyMetrics)},
        {"Thread local variables", num_threads * 256},
    };
    
    metrics->update_memory_analysis(data_structures, num_threads);
    
    std::vector<std::thread> threads;
    
    for (std::size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back([i, operations_per_thread, &shared_mutex, &shared_counter, &shared_data, metrics]() {
            std::hash<std::size_t> hasher;
            
            for (std::size_t j = 0; j < operations_per_thread; ++j) {
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
            std::cout << "C++ Enhanced Mutex thread " << i << " finished\n";
        });
    }
    
    for (auto& t : threads) t.join();
    
    auto final_counter = shared_counter.load();
    auto final_vec_size = shared_data.size();
    auto final_vec_capacity = shared_data.capacity();
    
    std::vector<std::pair<std::string, size_t>> final_data_structures = {
        {"Final std::vector<int32_t> actual", final_vec_size * sizeof(std::int32_t)},
        {"Final std::vector<int32_t> capacity", final_vec_capacity * sizeof(std::int32_t)},
        {"Memory fragmentation overhead", 2048},
        {"C++ runtime cleanup overhead", 1024},
    };
    metrics->update_memory_analysis(final_data_structures, 1);
    
    std::cout << "\nENHANCED MUTEX BENCHMARK SUMMARY:\n";
    std::cout << "  Final counter value: " << final_counter << "\n";
    std::cout << "  Final vector size: " << final_vec_size << " (capacity: " << final_vec_capacity << ")\n";
    std::cout << "  Vector memory usage: " << (final_vec_capacity * sizeof(std::int32_t)) / 1024.0 << " KB\n";
    std::cout << "  Vector efficiency: " << (final_vec_size * 100.0 / final_vec_capacity) << "%\n";
    
    metrics->print_comprehensive_results("Enhanced Shared Data Mutex");
}

std::string get_os_info() {
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

std::string get_cpu_architecture() {
#if defined(__APPLE__) && defined(__aarch64__)
    return "Apple Silicon (ARM64)";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__aarch64__)
    return "ARM64";
#elif defined(__arm__)
    return "ARM (32-bit)";
#elif defined(__i386__)
    return "x86 (32-bit)";
#else
    return "Unknown";
#endif
}

int main() {
    auto system_cores = std::thread::hardware_concurrency();
    
    std::cout << std::string(80, '=') << "\n";
    std::cout << "C++ ENHANCED CONCURRENCY BENCHMARK WITH MEMORY ANALYSIS\n";
    std::cout << std::string(80, '=') << "\n";
    
    std::cout << "\nPLATFORM INFORMATION:\n";
    std::cout << "  Operating System: " << get_os_info() << "\n";
    std::cout << "  CPU Architecture: " << get_cpu_architecture() << "\n";
    std::cout << "  Available CPU cores: " << system_cores << "\n";
    
    std::cout << "\nMEMORY ANALYSIS CAPABILITIES:\n";
#ifdef __linux__
    std::cout << "  RSS measurement: /proc/self/status (Linux)\n";
#elif defined(__APPLE__)
    std::cout << "  RSS measurement: mach task_info (macOS)\n";
#elif defined(_WIN32)
    std::cout << "  RSS measurement: Process Memory Counters (Windows)\n";
#endif
    std::cout << "  Heap estimation: Per data structure analysis\n";
    std::cout << "  Thread overhead: Stack + metadata + C++ overhead\n";
    std::cout << "  Runtime overhead: C++ runtime analysis\n";
    std::cout << "  Memory efficiency: Ratio analysis\n";
    
    std::cout << "\nCOMPILATION PROFILE:\n";
#ifdef NDEBUG
    std::cout << "  Build mode: Release (optimized)\n";
#else
    std::cout << "  Build mode: Debug (WARNING: Use -O3 -DNDEBUG for production benchmarks!)\n";
#endif
    
    const size_t test_threads = std::min(system_cores, 8u);
    const size_t producers_consumers = std::max(test_threads / 2, static_cast<size_t>(1));
    const size_t items_per_test = 5000;
    
    std::cout << "\nTEST CONFIGURATION:\n";
    std::cout << "  Threads per test: " << test_threads << "\n";
    std::cout << "  Producers/Consumers: " << producers_consumers << " each\n";
    std::cout << "  Items per producer: " << items_per_test << "\n";
    
    enhanced_producer_consumer_channel_benchmark(producers_consumers, producers_consumers, items_per_test);
    
    enhanced_shared_data_mutex_benchmark(test_threads, items_per_test);
    
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "C++ ENHANCED BENCHMARK COMPLETED\n";
    std::cout << std::string(80, '=') << "\n";
    
    std::cout << "\nKEY IMPROVEMENTS OVER BASIC VERSION:\n";
    std::cout << "  ✓ Real process RSS memory measurement from OS\n";
    std::cout << "  ✓ Detailed heap usage breakdown per data structure\n";
    std::cout << "  ✓ Thread overhead calculation (stack + metadata + C++ runtime)\n";
    std::cout << "  ✓ Runtime overhead analysis\n";
    std::cout << "  ✓ Memory efficiency ratios\n";
    std::cout << "  ✓ Cross-platform memory measurement (Linux/macOS/Windows)\n";
    std::cout << "  ✓ Comprehensive memory analysis comparable to Rust version\n";
    
    return 0;
}