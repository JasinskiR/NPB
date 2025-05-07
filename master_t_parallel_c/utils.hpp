#pragma once

#include <chrono>
#include <string>
#include <random>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <vector>
#include <span>
#include <concepts>
#include <thread>
#include <future>
#include <algorithm>
#include <cstdlib>

namespace npb {
namespace utils {

// Get number of threads from environment or use hardware concurrency
inline int get_num_threads() {
    int num_threads = 0;
    
    // Check for NPB_NUM_THREADS environment variable
    if (const char* env_threads = std::getenv("NPB_NUM_THREADS")) {
        num_threads = std::atoi(env_threads);
    }
    
    // If not set or invalid, check for OMP_NUM_THREADS
    if (num_threads <= 0) {
        if (const char* env_threads = std::getenv("OMP_NUM_THREADS")) {
            num_threads = std::atoi(env_threads);
        }
    }
    
    // If still not set or invalid, use hardware concurrency
    if (num_threads <= 0) {
        num_threads = std::thread::hardware_concurrency();
    }
    
    // Fallback to 1 if all else fails
    return num_threads > 0 ? num_threads : 1;
}

// Random number generator with same properties as original NPB
class RandomGenerator {
public:
    static constexpr double r23 = 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 
                              0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5;
    static constexpr double r46 = r23 * r23;
    static constexpr double t23 = 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 
                              2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0;
    static constexpr double t46 = t23 * t23;

    // Generate a random number using the linear congruential generator
    static double randlc(double* x, double a) {
        double t1, t2, t3, t4, a1, a2, x1, x2, z;

        // Break A into two parts such that A = 2^23 * A1 + A2
        t1 = r23 * a;
        a1 = static_cast<int>(t1);
        a2 = a - t23 * a1;

        // Break X into two parts such that X = 2^23 * X1 + X2
        t1 = r23 * (*x);
        x1 = static_cast<int>(t1);
        x2 = (*x) - t23 * x1;
        
        // Compute Z = A1 * X2 + A2 * X1 (mod 2^23)
        t1 = a1 * x2 + a2 * x1;
        t2 = static_cast<int>(r23 * t1);
        z = t1 - t23 * t2;
        
        // Compute X = 2^23 * Z + A2 * X2 (mod 2^46)
        t3 = t23 * z + a2 * x2;
        t4 = static_cast<int>(r46 * t3);
        (*x) = t3 - t46 * t4;

        return (r46 * (*x));
    }

    // Generate N random numbers
    static void vranlc(int n, double* x_seed, double a, std::span<double> y) {
        double x, t1, t2, t3, t4, a1, a2, x1, x2, z;

        // Break A into two parts such that A = 2^23 * A1 + A2
        t1 = r23 * a;
        a1 = static_cast<int>(t1);
        a2 = a - t23 * a1;
        x = *x_seed;

        for(int i = 0; i < n; i++) {
            // Break X into two parts such that X = 2^23 * X1 + X2
            t1 = r23 * x;
            x1 = static_cast<int>(t1);
            x2 = x - t23 * x1;
            
            // Compute Z = A1 * X2 + A2 * X1 (mod 2^23)
            t1 = a1 * x2 + a2 * x1;
            t2 = static_cast<int>(r23 * t1);
            z = t1 - t23 * t2;
            
            // Compute X = 2^23 * Z + A2 * X2 (mod 2^46)
            t3 = t23 * z + a2 * x2;
            t4 = static_cast<int>(r46 * t3);
            x = t3 - t46 * t4;
            
            y[i] = r46 * x;
        }
        *x_seed = x;
    }
};

// High-resolution timer
class Timer {
    public:
        Timer() {
            reset();
        }
    
        void start() {
            if (!running_) {
                start_time_ = std::chrono::high_resolution_clock::now();
                running_ = true;
            }
        }
    
        void stop() {
            if (running_) {
                auto end_time = std::chrono::high_resolution_clock::now();
                elapsed_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_).count();
                running_ = false;
            }
        }
    
        void reset() {
            elapsed_ns_ = 0;
            running_ = false;
        }
    
        [[nodiscard]] double elapsed() const {
            if (running_) {
                auto current_time = std::chrono::high_resolution_clock::now();
                return (elapsed_ns_ + std::chrono::duration_cast<std::chrono::nanoseconds>(
                        current_time - start_time_).count()) / 1e9;
            }
            return elapsed_ns_ / 1e9;
        }
    
        [[nodiscard]] int64_t elapsed_ns() const {
            if (running_) {
                auto current_time = std::chrono::high_resolution_clock::now();
                return elapsed_ns_ + std::chrono::duration_cast<std::chrono::nanoseconds>(
                        current_time - start_time_).count();
            }
            return elapsed_ns_;
        }
    
    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
        int64_t elapsed_ns_{0};
        bool running_{false};
    };
    class TimerManager {
        public:
            enum TimerID {
                T_INIT,
                T_BENCH,
                T_CONJ_GRAD,
                T_LAST
            };
        
            void clear(TimerID id) {
                if (id < T_LAST) {
                    timers_[id].reset();
                }
            }
        
            void start(TimerID id) {
                if (id < T_LAST) {
                    timers_[id].start();
                }
            }
        
            void stop(TimerID id) {
                if (id < T_LAST) {
                    timers_[id].stop();
                }
            }
        
            [[nodiscard]] double read(TimerID id) const {
                if (id < T_LAST) {
                    return timers_[id].elapsed();
                }
                return 0.0;
            }
        
            [[nodiscard]] int64_t read_ns(TimerID id) const {
                if (id < T_LAST) {
                    return timers_[id].elapsed_ns();
                }
                return 0;
            }
        
            [[nodiscard]] bool is_enabled() const {
                return enabled_;
            }
        
            void enable() {
                enabled_ = true;
            }
        
        private:
            Timer timers_[T_LAST];
            bool enabled_{false};
        };

// Function to print benchmark results
void print_results(
    const std::string& name,
    char class_type,
    int64_t n1,
    int64_t n2,
    int64_t n3,
    int64_t niter,
    double time,
    int64_t time_ns,
    double mops,
    const std::string& optype,
    bool verified,
    int num_threads = std::thread::hardware_concurrency(),
    bool with_timers = false,
    const std::vector<double>& timers = {}
);

// Templated utility functions for parallel operations
template <std::floating_point T, typename Func>
T parallel_sum(std::span<const T> data, Func transform) {
    const size_t hardware_threads = get_num_threads();
    const size_t num_threads = std::min(hardware_threads, data.size() / 1000 + 1);
    
    if (num_threads <= 1) {
        return std::transform_reduce(data.begin(), data.end(), T{}, std::plus<T>{}, transform);
    }

    std::vector<std::future<T>> futures(num_threads);
    std::vector<T> results(num_threads);
    
    const size_t block_size = data.size() / num_threads;
    
    for (size_t i = 0; i < num_threads; ++i) {
        size_t start = i * block_size;
        size_t end = (i == num_threads - 1) ? data.size() : (i + 1) * block_size;
        
        futures[i] = std::async(std::launch::async, [&, start, end, i]() {
            T local_sum{};
            for (size_t j = start; j < end; ++j) {
                local_sum += transform(data[j]);
            }
            return local_sum;
        });
    }
    
    T sum{};
    for (auto& future : futures) {
        sum += future.get();
    }
    
    return sum;
}

template <std::floating_point T>
T parallel_sum(std::span<const T> data) {
    return parallel_sum(data, [](T x) { return x; });
}

template <std::floating_point T, typename Func>
void parallel_for(size_t start, size_t end, Func func) {
    const size_t hardware_threads = get_num_threads();
    const size_t num_elements = end - start;
    const size_t num_threads = std::min(hardware_threads, num_elements / 1000 + 1);
    
    if (num_threads <= 1) {
        for (size_t i = start; i < end; ++i) {
            func(i);
        }
        return;
    }
    
    std::vector<std::thread> threads(num_threads);
    const size_t block_size = num_elements / num_threads;
    
    for (size_t t = 0; t < num_threads; ++t) {
        size_t block_start = start + t * block_size;
        size_t block_end = (t == num_threads - 1) ? end : block_start + block_size;
        
        threads[t] = std::thread([func, block_start, block_end]() {
            for (size_t i = block_start; i < block_end; ++i) {
                func(i);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
}

}
}