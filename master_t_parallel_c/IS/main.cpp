// main.cpp
#include "is.hpp"
#include <iostream>
#include <cstdlib>
#include <omp.h>

int main(int argc, char** argv) {
    // Load parameters
    auto params = npb::is::load_parameters();
    
    // Display header
    std::cout << "\n\n NAS Parallel Benchmarks 4.1 Modern C++23 with OpenMP - IS Benchmark\n\n";
    std::cout << " Size: " << params.total_keys << " (class " << params.class_id << ")\n";
    std::cout << " Iterations: " << params.iterations << "\n\n";
    
    // Create and run benchmark
    npb::is::IntegerSort is(params);
    is.run();
    
    // Print results
    npb::is::print_results(is, params, "IS", "keys ranked");
    
    return 0;
}