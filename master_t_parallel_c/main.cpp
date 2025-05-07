#include "cg.hpp"
#include "utils.hpp"

#include <iostream>
#include <iomanip>
#include <string>
#include <fstream>
#include <chrono>
#include <vector>
#include <cstdlib>

int main(int argc, char** argv) {
    // Set problem parameters based on class
    char problem_class = 'A';  // Default class
    
    // Check for CLASS environment variable
    if (const char* env_class = std::getenv("CLASS")) {
        problem_class = env_class[0];
    }
    
    // Parse command line arguments if provided
    if (argc > 1) {
        // Allow command line override of environment variable
        problem_class = argv[1][0];
    }
    
    // Setup problem parameters based on class
    npb::cg::Problem params;
    params.problem_class = problem_class;
    
    // Set thread count from environment or auto-detect
    params.num_threads = npb::utils::get_num_threads();
    
    switch (problem_class) {
        case 'S':
            params.na = 1400;
            params.nonzer = 7;
            params.max_iter = 15;
            params.shift = 10.0;
            params.rcond = 0.1;
            break;
        case 'W':
            params.na = 7000;
            params.nonzer = 8;
            params.max_iter = 15;
            params.shift = 12.0;
            params.rcond = 0.1;
            break;
        case 'A':
            params.na = 14000;
            params.nonzer = 11;
            params.max_iter = 15;
            params.shift = 20.0;
            params.rcond = 0.1;
            break;
        case 'B':
            params.na = 75000;
            params.nonzer = 13;
            params.max_iter = 75;
            params.shift = 60.0;
            params.rcond = 0.1;
            break;
        case 'C':
            params.na = 150000;
            params.nonzer = 15;
            params.max_iter = 75;
            params.shift = 110.0;
            params.rcond = 0.1;
            break;
        case 'D':
            params.na = 1500000;
            params.nonzer = 21;
            params.max_iter = 100;
            params.shift = 500.0;
            params.rcond = 0.1;
            break;
        case 'E':
            params.na = 9000000;
            params.nonzer = 26;
            params.max_iter = 100;
            params.shift = 1500.0;
            params.rcond = 0.1;
            break;
        default:
            std::cerr << "Invalid problem class: " << problem_class << std::endl;
            std::cerr << "Valid classes are S, W, A, B, C, D, E" << std::endl;
            return 1;
    }
    
    // Print benchmark information
    std::cout << "\n\n NAS Parallel Benchmarks C++23 version - CG Benchmark\n\n";
    std::cout << " Size: " << std::setw(11) << params.na << "\n";
    std::cout << " Iterations: " << std::setw(5) << params.max_iter << "\n";
    std::cout << " Threads: " << std::setw(10) << params.num_threads << "\n";
    
    // Enable timer for initialization
    npb::utils::TimerManager timer;
    timer.enable();
    timer.start(npb::utils::TimerManager::T_INIT);
    
    // Create the sparse matrix
    npb::cg::SparseMatrix matrix(params);
    
    timer.stop(npb::utils::TimerManager::T_INIT);
    std::cout << " Initialization time = " << std::setw(15) << std::fixed << std::setprecision(3) 
              << timer.read(npb::utils::TimerManager::T_INIT) << " seconds\n";
    
    // Run the benchmark
    timer.start(npb::utils::TimerManager::T_BENCH);
    double execution_time = matrix.run_benchmark();
    timer.stop(npb::utils::TimerManager::T_BENCH);
    
    // Verify the results
    bool verified = matrix.verify();
    
    std::cout << "\n Benchmark completed\n";
    
    if (params.problem_class != 'U') {
        double zeta_verify_value = matrix.get_zeta_verify_value();
        double zeta = matrix.get_zeta();
        double err = std::abs((zeta - zeta_verify_value) / zeta_verify_value);
        
        if (verified) {
            std::cout << " VERIFICATION SUCCESSFUL\n";
            std::cout << " Zeta is    " << std::setw(20) << std::scientific << std::setprecision(13) << zeta << "\n";
            std::cout << " Error is   " << std::setw(20) << std::scientific << std::setprecision(13) << err << "\n";
        } else {
            std::cout << " VERIFICATION FAILED\n";
            std::cout << " Zeta                " << std::setw(20) << std::scientific << std::setprecision(13) << zeta << "\n";
            std::cout << " The correct zeta is " << std::setw(20) << std::scientific << std::setprecision(13) << zeta_verify_value << "\n";
        }
    } else {
        std::cout << " Problem size unknown\n";
        std::cout << " NO VERIFICATION PERFORMED\n";
    }
    
    // Calculate and print MFLOPS
    double mflops = matrix.get_mflops(execution_time);
    
    // Print results
    npb::utils::print_results(
        "CG",
        params.problem_class,
        params.na,
        0,
        0,
        params.max_iter,
        execution_time,
        mflops,
        "floating point",
        verified
    );
    
    // Print timer information
    if (timer.is_enabled()) {
        double tmax = timer.read(npb::utils::TimerManager::T_BENCH);
        if (tmax == 0.0) tmax = 1.0;
        
        std::cout << "  SECTION   Time (secs)\n";
        
        double t = timer.read(npb::utils::TimerManager::T_INIT);
        std::cout << "  init:     " << std::setw(9) << std::fixed << std::setprecision(3) << t << "\n";
        
        t = timer.read(npb::utils::TimerManager::T_BENCH);
        std::cout << "  benchmark:" << std::setw(9) << std::fixed << std::setprecision(3) << t 
                  << "  (" << std::setw(6) << std::fixed << std::setprecision(2) << t*100.0/tmax << "%)\n";
        
        t = timer.read(npb::utils::TimerManager::T_CONJ_GRAD);
        std::cout << "  conj_grad:" << std::setw(9) << std::fixed << std::setprecision(3) << t 
                  << "  (" << std::setw(6) << std::fixed << std::setprecision(2) << t*100.0/tmax << "%)\n";
        
        t = tmax - t;
        std::cout << "  rest:     " << std::setw(9) << std::fixed << std::setprecision(3) << t 
                  << "  (" << std::setw(6) << std::fixed << std::setprecision(2) << t*100.0/tmax << "%)\n";
    }
    
    return 0;
}