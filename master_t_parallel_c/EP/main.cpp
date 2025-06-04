#include "ep.hpp"
#include "utils.hpp"

#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <cstdlib>

int main(int argc, char** argv) {
    // Set problem parameters based on class
    char problem_class = 'S';  // Default class
    
    // Check for CLASS environment variable
    if (const char* env_class = std::getenv("CLASS")) {
        problem_class = env_class[0];
    }
    
    // Get thread count from environment or auto-detect
    int num_threads = npb::utils::get_num_threads();
    
    // Parse command line arguments if provided
    if (argc >= 2) {
        // Check if the first argument is a single character class identifier
        if (strlen(argv[1]) == 1) {
            char c = argv[1][0];
            if (c == 'S' || c == 'W' || c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'E') {
                problem_class = c;
                
                // If there's a second argument, treat it as thread count
                if (argc >= 3 && isdigit(argv[2][0])) {
                    num_threads = std::atoi(argv[2]);
                }
            }
        }
    }
    
    // Process additional options if present
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            // Process options
            if ((argv[i][1] == 't' || strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) && i + 1 < argc) {
                num_threads = std::atoi(argv[i+1]);
                i++; // Skip the next argument as it's the thread count value
            } else if (strcmp(argv[i], "--no-header") == 0) {
                // Skip header printing - handled by the utility function
            } else if ((argv[i][1] == 'c' || strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--class") == 0) && i + 1 < argc) {
                problem_class = argv[i+1][0];
                i++; // Skip the next argument as it's the class value
            }
        } else if (strlen(argv[i]) == 1 && i == 1) {
            // Already handled above for the first argument
            continue;
        } else if (strncmp(argv[i], "CLASS=", 6) == 0) {
            // Legacy style: CLASS=X
            problem_class = argv[i][6];
        }
    }
    
    // Validate class type
    if (problem_class != 'S' && problem_class != 'W' && 
        problem_class != 'A' && problem_class != 'B' && 
        problem_class != 'C' && problem_class != 'D' && 
        problem_class != 'E') {
        std::cerr << "Invalid problem class: " << problem_class << std::endl;
        std::cerr << "Valid classes are S, W, A, B, C, D, E" << std::endl;
        return 1;
    }
    
    // Setup problem parameters based on class
    int m;
    int iterations = 1; // EP only has one iteration
    
    // Set parameters based on the NAS problem sizes
    switch (problem_class) {
        case 'S': m = 24; break;
        case 'W': m = 25; break;
        case 'A': m = 28; break;
        case 'B': m = 30; break;
        case 'C': m = 32; break;
        case 'D': m = 36; break;
        case 'E': m = 40; break;
        default:
            std::cerr << "Invalid problem class" << std::endl;
            return 1;
    }
    
    // Print benchmark information
    std::cout << "\n\n NAS Parallel Benchmarks C++ version - EP Benchmark\n\n";
    std::cout << " Size: 2^" << std::setw(2) << m << " random numbers\n";
    std::cout << " Threads: " << std::setw(10) << num_threads << "\n";
    
    // Enable timer for initialization
    npb::utils::TimerManager timer;
    timer.enable();
    timer.start(npb::utils::TimerManager::T_INIT);
    
    // Create and initialize the benchmark
    npb::EPBenchmark benchmark(problem_class, num_threads);
    
    timer.stop(npb::utils::TimerManager::T_INIT);
    std::cout << " Initialization time = " << std::setw(15) << std::fixed << std::setprecision(3) 
              << timer.read(npb::utils::TimerManager::T_INIT) << " seconds ("
              << timer.read_ns(npb::utils::TimerManager::T_INIT) << " ns)\n";
    
    // Run the benchmark
    timer.start(npb::utils::TimerManager::T_BENCH);
    benchmark.run();
    timer.stop(npb::utils::TimerManager::T_BENCH);
    
    double execution_time = timer.read(npb::utils::TimerManager::T_BENCH);
    int64_t execution_time_ns = timer.read_ns(npb::utils::TimerManager::T_BENCH);
    
    // Verify the results
    bool verified = benchmark.verify();
    
    std::cout << "\n Benchmark completed\n";
    
    if (verified) {
        std::cout << " VERIFICATION SUCCESSFUL\n";
    } else {
        std::cout << " VERIFICATION FAILED\n";
    }
    
    // Calculate and print MOPS
    double mops = benchmark.get_mops();
    
    // Print results
    npb::utils::print_results(
        "EP",
        problem_class,
        1 << m,     // Size - number of random numbers (2^m)
        0,          // n2 - not used in EP
        0,          // n3 - not used in EP
        iterations, // Number of iterations
        execution_time,
        execution_time_ns,
        mops,
        "Random number generation",
        verified,
        num_threads
    );
    
    // Print timer information
    if (timer.is_enabled()) {
        double tmax = timer.read(npb::utils::TimerManager::T_BENCH);
        int64_t tmax_ns = timer.read_ns(npb::utils::TimerManager::T_BENCH);
        if (tmax == 0.0) tmax = 1.0;
        if (tmax_ns == 0) tmax_ns = 1;
        
        std::cout << "  SECTION   Time (secs)       Time (ns)\n";
        
        double t = timer.read(npb::utils::TimerManager::T_INIT);
        int64_t t_ns = timer.read_ns(npb::utils::TimerManager::T_INIT);
        std::cout << "  init:     " << std::setw(9) << std::fixed << std::setprecision(3) << t 
                  << "  " << std::setw(15) << t_ns << "\n";
        
        t = timer.read(npb::utils::TimerManager::T_BENCH);
        t_ns = timer.read_ns(npb::utils::TimerManager::T_BENCH);
        std::cout << "  benchmark:" << std::setw(9) << std::fixed << std::setprecision(3) << t 
                  << "  " << std::setw(15) << t_ns
                  << "  (" << std::setw(6) << std::fixed << std::setprecision(2) << t*100.0/tmax << "%)\n";
    }
    
    return 0;
}