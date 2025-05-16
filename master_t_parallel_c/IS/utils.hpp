#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <omp.h>
#include <concepts>
#include <cmath>
#include <random>
#include <algorithm>
#include <ranges>
#include <span>
#include <array>
#include <vector>
#include <execution>
#include <sstream>

namespace npb {
namespace utils {

// Timer Manager class for benchmarking
class TimerManager {
public:
    using TimerID = int;
    static constexpr int MAX_TIMERS = 64;
    
    TimerManager() : timer_values_{}, enabled_(false) {
        // Check if timer flag file exists
        FILE* fp;
        if ((fp = fopen("timer.flag", "r")) != nullptr) {
            enabled_ = true;
            fclose(fp);
        }
    }
    
    void clear(TimerID id) {
        if (id >= 0 && id < MAX_TIMERS) {
            timer_values_[id] = 0.0;
        }
    }
    
    void start(TimerID id) {
        if (enabled_ && id >= 0 && id < MAX_TIMERS) {
            timer_values_[id] -= omp_get_wtime();
        }
    }
    
    void stop(TimerID id) {
        if (enabled_ && id >= 0 && id < MAX_TIMERS) {
            timer_values_[id] += omp_get_wtime();
        }
    }
    
    double read(TimerID id) const {
        if (id >= 0 && id < MAX_TIMERS) {
            return timer_values_[id];
        }
        return 0.0;
    }
    
    bool is_enabled() const {
        return enabled_;
    }
    
private:
    std::array<double, MAX_TIMERS> timer_values_;
    bool enabled_;
};

// Random number generation with consistent results
class RandomGenerator {
public:
    static double randlc(double* x, double a) {
        // Use pre-computed constants instead of std::pow
        static const double r23 = 1.1920928955078125e-07; // 0.5^23
        static const double r46 = 1.4210854715202004e-14; // 0.5^46
        static const double t23 = 8388608.0;             // 2^23
        static const double t46 = 7.0368744177664e+13;   // 2^46
        
        double t1, t2, t3, t4, a1, a2, x1, x2, z;
        
        // Break A into two parts
        t1 = r23 * a;
        a1 = static_cast<int>(t1);
        a2 = a - t23 * a1;
        
        // Break X into two parts
        t1 = r23 * (*x);
        x1 = static_cast<int>(t1);
        x2 = (*x) - t23 * x1;
        
        // Compute Z = A1 * X2 + A2 * X1
        t1 = a1 * x2 + a2 * x1;
        t2 = static_cast<int>(r23 * t1);
        z = t1 - t23 * t2;
        
        // Compute X = 2^23 * Z + A2 * X2
        t3 = t23 * z + a2 * x2;
        t4 = static_cast<int>(r46 * t3);
        *x = t3 - t46 * t4;
        
        return (r46 * (*x));
    }
    
    static void vranlc(int n, double* x_seed, double a, std::span<double> y) {
        // Use pre-computed constants instead of std::pow
        static const double r23 = 1.1920928955078125e-07; // 0.5^23
        static const double r46 = 1.4210854715202004e-14; // 0.5^46 
        static const double t23 = 8388608.0;             // 2^23
        static const double t46 = 7.0368744177664e+13;   // 2^46
        
        double t1, t2, t3, t4, a1, a2, x1, x2, z;
        
        // Break A into two parts
        t1 = r23 * a;
        a1 = static_cast<int>(t1);
        a2 = a - t23 * a1;
        
        double x = *x_seed;
        
        // Generate N results
        for (int i = 0; i < n; i++) {
            // Break X into two parts
            t1 = r23 * x;
            x1 = static_cast<int>(t1);
            x2 = x - t23 * x1;
            
            // Compute Z = A1 * X2 + A2 * X1
            t1 = a1 * x2 + a2 * x1;
            t2 = static_cast<int>(r23 * t1);
            z = t1 - t23 * t2;
            
            // Compute X = 2^23 * Z + A2 * X2
            t3 = t23 * z + a2 * x2;
            t4 = static_cast<int>(r46 * t3);
            x = t3 - t46 * t4;
            
            y[i] = r46 * x;
        }
        
        *x_seed = x;
    }
};

// Timer functions
void timer_clear(int id);
void timer_start(int id);
void timer_stop(int id);
double timer_read(int id);

// Random number generator functions
double randlc(double* x, double a);
void vranlc(int n, double* x_seed, double a, double* y);
void vranlc(int n, double* x_seed, double a, std::vector<double>& y);

// Results printing function
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
    int num_threads,
    bool with_timers = false,
    const std::vector<double>& timers = {}
);

// Memory allocation helper
template<typename T>
T* allocate_memory(size_t size) {
    T* ptr = static_cast<T*>(std::malloc(sizeof(T) * size));
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

// Modern parallel sorting using C++23 features
template<typename RandomIt>
void parallel_sort(RandomIt first, RandomIt last) {
    // Fall back to regular sort as par_unseq is not available
    #if defined(__cpp_lib_parallel_algorithm) && __cpp_lib_parallel_algorithm >= 201603
        std::sort(std::execution::par, first, last);
    #else
        std::sort(first, last);
    #endif
}

// Integer conversion used in random number generation
inline int icnvrt(double x, int ipwr2) {
    return static_cast<int>(ipwr2 * x);
}

} // namespace utils
} // namespace npb