#include "is.hpp"
#include <iostream>
#include <cstdlib>
#include <omp.h>
#include <string>
#include <cctype>
#include <stdexcept>
#include <iomanip>

int main(int argc, char** argv) {
    char class_id = 'S'; // Default class
    int num_threads = omp_get_max_threads(); // Default to max threads
    
    // Process command line arguments
    if (argc > 1) {
        std::string class_arg_str = argv[1];
        if (class_arg_str.length() == 1 && std::isalpha(class_arg_str[0])) {
            class_id = std::toupper(class_arg_str[0]);
        } else {
            std::cerr << "Warning: Invalid class argument '" << class_arg_str << "'. Using default class '" << class_id << "'." << std::endl;
        }
    }

    if (argc > 2) {
        try {
            num_threads = std::stoi(argv[2]);
            if (num_threads <= 0) {
                std::cerr << "Warning: Number of threads must be positive (" << argv[2] << "). Using default: " << omp_get_max_threads() << std::endl;
                num_threads = omp_get_max_threads();
            }
        } catch (const std::invalid_argument& ia) {
            std::cerr << "Warning: Invalid argument for number of threads '" << argv[2] << "'. Not a number. Using default: " << omp_get_max_threads() << std::endl;
            num_threads = omp_get_max_threads();
        } catch (const std::out_of_range& oor) {
            std::cerr << "Warning: Number of threads '" << argv[2] << "' out of range. Using default: " << omp_get_max_threads() << std::endl;
            num_threads = omp_get_max_threads();
        }
    }
    
    omp_set_num_threads(num_threads);
    
    auto params = npb::is::load_parameters<int64_t>(class_id);
    
    // Create the IntegerSort object first
    npb::is::IntegerSort<int64_t> is(params);
    
    std::cout << "\n\n NAS Parallel Benchmarks 4.1 Modern C++20 with OpenMP - IS Benchmark\n\n";
    std::cout << " Class: " << class_id << "\n";
    std::cout << " Size: " << params.total_keys << "\n";
    std::cout << " Iterations: " << params.iterations << "\n";
    std::cout << " Threads requested: " << num_threads << ", Threads used: " << omp_get_num_threads() << "\n";
    std::cout << " Using bucket sort: " << (is.getUseBuckets() ? "YES" : "NO") << "\n\n";
    
    // Now we can reference the 'is' object because it's already initialized
    std::cout << " Initialization time =           " << std::fixed << std::setprecision(3) 
              << is.getTimer(params.T_INITIALIZATION) << " seconds (" 
              << static_cast<int64_t>(is.getTimer(params.T_INITIALIZATION) * 1e9) << " ns)\n";
    std::cout << " Initialization complete\n\n";
    std::cout << " IS Benchmark Results:\n\n";
    
    // Run the benchmark
    is.run();
    
    // Print results
    npb::is::print_results(is, params, "IS", "keys ranked");
    
    return 0;
}