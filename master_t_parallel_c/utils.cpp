#include "utils.hpp"

#include <iostream>
#include <iomanip>
#include <ctime>
#include <string>
#include <vector>

namespace npb::utils {

void print_results(
    const std::string& name,
    char class_type,
    int64_t n1,
    int64_t n2,
    int64_t n3,
    int64_t niter,
    double time,
    double mops,
    const std::string& optype,
    bool verified,
    bool with_timers,
    const std::vector<double>& timers
) {
    std::cout << "\n\n " << name << " Benchmark Completed\n";
    std::cout << " Class          =                        " << class_type << "\n";
    
    if (name == "IS") {
        if (n3 == 0) {
            int64_t nn = n1;
            if (n2 != 0) {
                nn *= n2;
            }
            std::cout << " Size            =             " << std::setw(12) << nn << "\n";
        } else {
            std::cout << " Size            =             " << std::setw(4) << n1 << "x"
                      << std::setw(4) << n2 << "x" << std::setw(4) << n3 << "\n";
        }
    } else {
        if (n2 == 0 && n3 == 0) {
            if (name == "EP") {
                std::string size_str = std::to_string(static_cast<int64_t>(std::pow(2.0, n1)));
                size_t j = size_str.length() - 1;
                if (size_str[j] == '.') {
                    size_str.erase(j);
                }
                std::cout << " Size            =          " << std::setw(15) << size_str << "\n";
            } else {
                std::cout << " Size            =             " << std::setw(12) << n1 << "\n";
            }
        } else {
            std::cout << " Size            =           " << std::setw(4) << n1 << "x"
                      << std::setw(4) << n2 << "x" << std::setw(4) << n3 << "\n";
        }
    }
    
    // Print number of threads
    std::cout << " Num threads     =             " << std::setw(12) << std::thread::hardware_concurrency() << "\n";
    
    // Print additional benchmark info
    std::cout << " Iterations      =             " << std::setw(12) << niter << "\n";
    std::cout << " Time in seconds =             " << std::setw(12) << std::fixed << std::setprecision(2) << time << "\n";
    std::cout << " Mop/s total     =             " << std::setw(12) << std::fixed << std::setprecision(2) << mops << "\n";
    std::cout << " Operation type  = " << std::setw(24) << optype << "\n";
    
    // Verification results
    if (verified) {
        std::cout << " Verification    =               SUCCESSFUL\n";
    } else {
        std::cout << " Verification    =             UNSUCCESSFUL\n";
    }
    
    // Compiler and system info
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    
    std::ostringstream compile_date_stream;
    compile_date_stream << std::put_time(&tm, "%d %b %Y");
    std::string compile_date = compile_date_stream.str();
    
    std::ostringstream version_info;
    #ifdef __clang__
        version_info << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__;
    #elif __GNUC__
        version_info << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
    #else
        version_info << "Unknown";
    #endif
    
    std::cout << " Version         =             " << std::setw(12) << "4.1" << "\n"; // NPB version
    std::cout << " Compile date    =             " << std::setw(12) << compile_date << "\n";
    std::cout << " Compiler ver    =             " << std::setw(12) << version_info.str() << "\n";
    std::cout << " C++ version     =             " << std::setw(12) << "C++23" << "\n";
    
    std::cout << "\n Compile options:\n";
    std::cout << "    CC           = g++ -std=c++23\n";
    std::cout << "    CFLAGS       = -O3 -march=native -fopenmp\n";
    
    // Print custom footer
    std::cout << "\n\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "    NPB-CPP (C++23 version) - CG Benchmark\n";
    std::cout << "    Modern C++ implementation with enhanced parallelism\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "\n";
}

} // namespace npb::utils