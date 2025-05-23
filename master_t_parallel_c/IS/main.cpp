// main.cpp
#include "is.hpp"
#include <iostream>
#include <cstdlib>
#include <omp.h>
#include <string>

int main(int argc, char** argv) {
    char class_id = 'S'; // Default class
    int num_threads = omp_get_max_threads();
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            // Handle flags
            if (argv[i][1] == 't' && i+1 < argc) {
                num_threads = std::atoi(argv[i+1]);
                i++;
            }
        } else if (strlen(argv[i]) == 1) {
            // Single character argument - assume it's the class
            class_id = toupper(argv[i][0]); // Ensure uppercase
        }
    }
    
    // Set number of threads
    omp_set_num_threads(num_threads);
    
    // Load parameters with the specified class
    auto params = npb::is::load_parameters(class_id);
    
    // Display header
    std::cout << "\n\n NAS Parallel Benchmarks 4.1 Modern C++23 with OpenMP - IS Benchmark\n\n";
    std::cout << " Size: " << params.total_keys << " (class " << params.class_id << ")\n";
    std::cout << " Iterations: " << params.iterations << "\n";
    std::cout << " Threads: " << omp_get_max_threads() << "\n";
    std::cout << " Using bucket sort: " << 
    #ifdef USE_BUCKETS
    "YES"
    #else
    "NO"
    #endif
    << "\n\n";
    
    // Create and run benchmark
    npb::is::IntegerSort is(params);
    is.run();
    
    // Print results
    npb::is::print_results(is, params, "IS", "keys ranked");
    
    return 0;
}