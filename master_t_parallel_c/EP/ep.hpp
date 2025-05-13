#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>

// Forward declaration for utils - updated to use npb::utils namespace
namespace npb {
namespace utils {
    double timer_read(int timer_id);
    void timer_clear(int timer_id);
    void timer_start(int timer_id);
    void timer_stop(int timer_id);
    double randlc(double* x, double a);
    void vranlc(int n, double* x_seed, double a, double* y);
}
}

namespace npb {

class EPBenchmark {
public:
    // Constructor with class type and thread count
    explicit EPBenchmark(char class_type, int num_threads = 1);
    
    // Run the benchmark
    void run();
    
    // Print results of the benchmark
    void print_results() const;
    
    // Verify the results
    bool verify() const;
    
    // Get benchmark statistics
    double get_mops() const;

private:
    // Benchmark parameters
    static constexpr int T_BENCHMARKING = 0;
    static constexpr int T_INITIALIZATION = 1;
    static constexpr int T_SORTING = 2;
    static constexpr int T_TOTAL_EXECUTION = 3;
    
    static constexpr double EPSILON = 1.0e-8;
    static constexpr double A = 1220703125.0;
    static constexpr double S = 271828183.0;
    
    // Parameters determined by class
    int M;                 // Log2 of number of random number pairs
    int MK = 16;           // Log2 of size of each batch
    int MM;                // M - MK
    std::int64_t NN;       // 1 << MM
    std::int64_t NK;       // 1 << MK
    int NQ = 10;           // Size of q array
    
    // Data structures
    std::vector<double> x; // Random number array
    std::vector<double> q; // Results array
    std::vector<int> key_array = {0, 0}; // Add this line for verification
    
    // Workload distribution
    int num_threads;
    
    // Results
    double sx = 0.0;
    double sy = 0.0;
    double gc = 0.0;
    double tm = 0.0;
    
    // Verification
    double sx_verify_value = 0.0;
    double sy_verify_value = 0.0;
    bool verified = false;
    bool timers_enabled = false;
    
    // Private methods
    void init();
    void compute_gaussian_pairs();
    bool verify_results();
    void set_verification_values();
    
    // Worker function for parallel execution
    void worker_task(int tid, int num_workers);
};

} // namespace npb