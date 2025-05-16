// is.hpp
#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <array>

namespace npb {
namespace is {

// Class constants and parameters
struct ISParameters {
    static constexpr int T_BENCHMARKING = 0;
    static constexpr int T_INITIALIZATION = 1;
    static constexpr int T_SORTING = 2;
    static constexpr int T_TOTAL_EXECUTION = 3;
    
    int total_keys;        // Number of keys
    int max_key;           // Maximum key value
    int num_buckets;       // Number of buckets for bucket sort
    int iterations;        // Number of iterations
    char class_id;         // Problem class ('S', 'W', 'A', 'B', 'C', 'D')
    
    // Verification values
    std::array<int64_t, 5> test_index_array;
    std::array<int64_t, 5> test_rank_array;
    static constexpr int TEST_ARRAY_SIZE = 5;
};

// Main Integer Sort class
class IntegerSort {
public:
    using KeyType = int64_t; // For class D and above, use 64-bit integers
    
    explicit IntegerSort(const ISParameters& params);
    ~IntegerSort();
    
    // Run the benchmark
    void run();
    
    // Get benchmark results
    double getExecutionTime() const;
    double getMopsTotal() const;
    bool getVerificationStatus() const;
    
private:
    // Core sorting function
    void rank(int iteration);
    
    // Verification function
    void full_verify();
    
    // Initialization functions
    void create_seq(double seed, double a);
    double find_my_seed(int kn, int np, int64_t nn, double s, double a);
    
    // Memory allocation
    void allocate_key_buffer();
    void* alloc_memory(size_t size);
    
    // Timer functions
    void timer_clear(int timer);
    void timer_start(int timer);
    void timer_stop(int timer);
    double timer_read(int timer) const;
    
    // Random number generation
    double randlc(double* x, double a);
    void vranlc(int n, double* x_seed, double a, double y[]);
    
    // Data members
    ISParameters params_;
    bool verified_ = false;
    int passed_verification_ = 0;
    double execution_time_ = 0.0;
    bool timers_enabled_ = false;
    
    // Main key arrays
    std::vector<KeyType> key_array_;      // Main array of keys
    std::vector<KeyType> key_buff1_;      // Work array for keys
    std::vector<KeyType> key_buff2_;      // Work array for keys
    std::vector<KeyType> partial_verify_vals_; // For partial verification
    KeyType* key_buff_ptr_global_ = nullptr; // For full verification
    
    // Bucket sort data
    #ifdef USE_BUCKETS
    std::vector<std::vector<KeyType>> bucket_size_;
    std::vector<KeyType> bucket_ptrs_;
    #else
    std::vector<KeyType*> key_buff1_aptr_;
    #endif
    
    // Timer data
    std::array<double, 4> timer_values_{};
};

// Parameter loading from npbparams.hpp
ISParameters load_parameters();

// Result reporting
void print_results(const IntegerSort& is, const ISParameters& params, 
                  const std::string& name, const std::string& optype);

} // namespace is
} // namespace npb