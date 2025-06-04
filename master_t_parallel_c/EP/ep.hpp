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
    explicit EPBenchmark(char class_type, int num_threads = 1);
    
    void run();
    void print_results() const;
    bool verify() const;
    double get_mops() const;

private:
    static constexpr int T_BENCHMARKING = 0;
    static constexpr int T_INITIALIZATION = 1;
    static constexpr int T_SORTING = 2;
    static constexpr int T_TOTAL_EXECUTION = 3;
    
    static constexpr double EPSILON = 1.0e-8;
    static constexpr double A = 1220703125.0;
    static constexpr double S = 271828183.0;
    
    int M;
    int MK = 16;
    int MM;
    std::int64_t NN;
    std::int64_t NK;
    int NQ = 10;
    
    std::vector<double> x;
    std::vector<double> q;
    
    int num_threads;
    std::int64_t k_offset;
    double an;
    
    double sx = 0.0;
    double sy = 0.0;
    double gc = 0.0;
    double tm = 0.0;
    
    double sx_verify_value = 0.0;
    double sy_verify_value = 0.0;
    bool verified = false;
    bool timers_enabled = false;
    
    void init();
    void compute_gaussian_pairs();
    bool verify_results();
    void set_verification_values();
    void worker_task(int tid, int num_workers);
};

}