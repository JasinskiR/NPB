#include "ep.hpp"
#include "utils.hpp"

#include <algorithm>
#include <chrono>
#include <execution>
#include <filesystem>
#include <format>
#include <iomanip>
#include <functional>

namespace npb {

EPBenchmark::EPBenchmark(char class_type, int num_threads) : q(NQ, 0.0) {
    switch (class_type) {
        case 'S': M = 24; break;
        case 'W': M = 25; break;
        case 'A': M = 28; break;
        case 'B': M = 30; break;
        case 'C': M = 32; break;
        case 'D': M = 36; break;
        case 'E': M = 40; break;
        default:
            throw std::invalid_argument("Invalid class type");
    }
    
    MM = M - MK;
    NN = static_cast<std::int64_t>(1) << MM;
    NK = static_cast<std::int64_t>(1) << MK;
    
    x.resize(2 * NK + 1);
    
    this->num_threads = num_threads;
    
    timers_enabled = std::filesystem::exists("timer.flag");
    
    set_verification_values();
}

bool EPBenchmark::verify() const {
    return verified;
}

double EPBenchmark::get_mops() const {
    return std::pow(2.0, M + 1) / tm / 1000000.0;
}

void EPBenchmark::set_verification_values() {
    switch (M) {
        case 24:
            sx_verify_value = -3.247834652034740e+3;
            sy_verify_value = -6.958407078382297e+3;
            break;
        case 25:
            sx_verify_value = -2.863319731645753e+3;
            sy_verify_value = -6.320053679109499e+3;
            break;
        case 28:
            sx_verify_value = -4.295875165629892e+3;
            sy_verify_value = -1.580732573678431e+4;
            break;
        case 30:
            sx_verify_value = 4.033815542441498e+4;
            sy_verify_value = -2.660669192809235e+4;
            break;
        case 32:
            sx_verify_value = 4.764367927995374e+4;
            sy_verify_value = -8.084072988043731e+4;
            break;
        case 36:
            sx_verify_value = 1.982481200946593e+5;
            sy_verify_value = -1.020596636361769e+5;
            break;
        case 40:
            sx_verify_value = -5.319717441530e+05;
            sy_verify_value = -3.688834557731e+05;
            break;
        default:
            verified = false;
    }
}

void EPBenchmark::init() {
    double dum[3] = {1.0, 1.0, 1.0};
    
    npb::utils::timer_clear(T_BENCHMARKING);
    if (timers_enabled) {
        npb::utils::timer_clear(T_INITIALIZATION);
        npb::utils::timer_clear(T_SORTING);
        npb::utils::timer_clear(T_TOTAL_EXECUTION);
    }
    
    if (timers_enabled) {
        npb::utils::timer_start(T_TOTAL_EXECUTION);
    }
    
    npb::utils::vranlc(0, &dum[0], dum[1], &dum[2]);
    dum[0] = npb::utils::randlc(&dum[1], dum[2]);
    
    std::fill(x.begin(), x.end(), -1.0e99);
    
    if (timers_enabled) {
        npb::utils::timer_start(T_INITIALIZATION);
    }
    
    double t1 = A;
    npb::utils::vranlc(0, &t1, A, x.data());
    
    t1 = A;
    for (int i = 0; i < MK + 1; i++) {
        double t2 = npb::utils::randlc(&t1, t1);
    }
    
    an = t1;
    
    if (timers_enabled) {
        npb::utils::timer_stop(T_INITIALIZATION);
    }
    
    std::cout << " Initialization complete\n";
}

void EPBenchmark::worker_task(int tid, int num_workers) {
    double local_sx = 0.0;
    double local_sy = 0.0;
    std::vector<double> local_q(NQ, 0.0);
    
    std::vector<double> x_vec(2 * NK);
    
    std::int64_t chunk_size = NN / num_workers;
    std::int64_t start_k = tid * chunk_size;
    std::int64_t end_k = (tid == num_workers - 1) ? NN : (tid + 1) * chunk_size;
    
    for (std::int64_t k = start_k; k < end_k; k++) {
        std::int64_t kk = k_offset + k + 1;
        double t1 = S;
        double t2 = an;
        
        for (int i = 1; i <= 100; i++) {
            std::int64_t ik = kk / 2;
            if ((2 * ik) != kk) {
                double t3 = npb::utils::randlc(&t1, t2);
            }
            if (ik == 0) break;
            double t3 = npb::utils::randlc(&t2, t2);
            kk = ik;
        }
        
        if (timers_enabled && tid == 0) {
            npb::utils::timer_start(T_SORTING);
        }
        npb::utils::vranlc(2 * NK, &t1, A, x_vec.data());
        if (timers_enabled && tid == 0) {
            npb::utils::timer_stop(T_SORTING);
        }
        
        for (int i = 0; i < NK; i++) {
            double x1 = 2.0 * x_vec[2*i] - 1.0;
            double x2 = 2.0 * x_vec[2*i+1] - 1.0;
            double t1_inner = x1 * x1 + x2 * x2;
            
            if (t1_inner <= 1.0) {
                double t2_inner = std::sqrt(-2.0 * std::log(t1_inner) / t1_inner);
                double t3 = x1 * t2_inner;
                double t4 = x2 * t2_inner;
                
                int l = std::max(std::abs(static_cast<int>(t3)), 
                               std::abs(static_cast<int>(t4)));
                if (l < NQ) {
                    local_q[l] += 1.0;
                    local_sx += t3;
                    local_sy += t4;
                }
            }
        }
    }
    
    static std::mutex mtx;
    {
        std::lock_guard<std::mutex> lock(mtx);
        sx += local_sx;
        sy += local_sy;
        for (int i = 0; i < NQ; i++) {
            q[i] += local_q[i];
        }
    }
}

void EPBenchmark::compute_gaussian_pairs() {
    sx = 0.0;
    sy = 0.0;
    std::fill(q.begin(), q.end(), 0.0);
    
    k_offset = -1;
    
    npb::utils::timer_start(T_BENCHMARKING);
    
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([this, i]() {
            this->worker_task(i, num_threads);
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    gc = std::accumulate(q.begin(), q.end(), 0.0);
    
    npb::utils::timer_stop(T_BENCHMARKING);
    tm = npb::utils::timer_read(T_BENCHMARKING);
}

bool EPBenchmark::verify_results() {
    double sx_err = std::fabs((sx - sx_verify_value) / sx_verify_value);
    double sy_err = std::fabs((sy - sy_verify_value) / sy_verify_value);
    
    verified = (sx_err <= EPSILON) && (sy_err <= EPSILON);
    
    return verified;
}

void EPBenchmark::run() {
    init();
    compute_gaussian_pairs();
    verify_results();
    print_results();
}

void EPBenchmark::print_results() const {
    double mflops = std::pow(2.0, M + 1) / tm / 1000000.0;
    
    std::cout << "\n EP Benchmark Results:\n\n";
    std::cout << " CPU Time = " << std::setw(10) << std::setprecision(4) << tm << "\n";
    std::cout << " N = 2^" << std::setw(5) << M << "\n";
    std::cout << " No. Gaussian Pairs = " << std::setw(15) << std::fixed << gc << "\n";
    std::cout << " Sums = " << std::setprecision(15) << std::setw(25) << sx
              << " " << std::setw(25) << sy << "\n";
    std::cout << " Counts: \n";
    
    for (int i = 0; i < NQ - 1; i++) {
        std::cout << std::setw(3) << i << std::setw(15) << q[i] << "\n";
    }
    
    std::cout << "\n Verification: " << (verified ? "SUCCESSFUL" : "FAILED") << "\n";
    
    double sx_err = std::fabs((sx - sx_verify_value) / sx_verify_value);
    double sy_err = std::fabs((sy - sy_verify_value) / sy_verify_value);
    
    std::cout << " Verification Details:\n";
    std::cout << std::scientific << std::setprecision(15);
    std::cout << " Calculated sx: " << sx << "\n";
    std::cout << " Expected sx:   " << sx_verify_value << "\n";
    std::cout << " Absolute diff: " << std::fabs(sx - sx_verify_value) << "\n";
    std::cout << " Relative diff: " << sx_err << " (threshold: " << EPSILON << ")\n\n";
    
    std::cout << " Calculated sy: " << sy << "\n";
    std::cout << " Expected sy:   " << sy_verify_value << "\n";
    std::cout << " Absolute diff: " << std::fabs(sy - sy_verify_value) << "\n";
    std::cout << " Relative diff: " << sy_err << " (threshold: " << EPSILON << ")\n";
    
    if (verified) {
        std::cout << "\n The sums matched the expected values.\n";
    } else {
        std::cout << "\n The sums did not match the expected values.\n";
        std::cout << " At least one relative error exceeds the threshold of " << EPSILON << "\n";
    }
    
    std::cout << std::fixed;
    
    std::cout << "\n Mop/s total = " << std::setw(12) << std::setprecision(2) << mflops << "\n";
    
    if (timers_enabled) {
        double tt = npb::utils::timer_read(T_TOTAL_EXECUTION);
        if (tt <= 0.0) tt = 1.0;
        
        std::cout << "\nAdditional timers -\n";
        std::cout << " Total execution: " << std::setw(9) << std::setprecision(3) << tt << "\n";
        
        double t = npb::utils::timer_read(T_INITIALIZATION);
        std::cout << " Initialization : " << std::setw(9) << std::setprecision(3) << t 
                  << " (" << std::setw(5) << std::setprecision(2) << (t * 100.0 / tt) << "%)\n";
        
        t = npb::utils::timer_read(T_BENCHMARKING);
        std::cout << " Benchmarking   : " << std::setw(9) << std::setprecision(3) << t 
                  << " (" << std::setw(5) << std::setprecision(2) << (t * 100.0 / tt) << "%)\n";
        
        t = npb::utils::timer_read(T_SORTING);
        std::cout << " Random numbers : " << std::setw(9) << std::setprecision(3) << t 
                  << " (" << std::setw(5) << std::setprecision(2) << (t * 100.0 / tt) << "%)\n";
    }
}

}