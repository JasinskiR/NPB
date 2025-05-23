// is.cpp
#include "is.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <execution>
#include <omp.h>
#include <cstring>
#include <format>
#include <ranges>
#include <memory>

// Add this below the includes 

// Make sure bucket sort is enabled
#ifndef USE_BUCKETS
#define USE_BUCKETS
#endif

namespace {
    // Local aliases for timer constants
    constexpr auto T_BENCHMARKING = npb::is::ISParameters::T_BENCHMARKING;
    constexpr auto T_INITIALIZATION = npb::is::ISParameters::T_INITIALIZATION;
    constexpr auto T_SORTING = npb::is::ISParameters::T_SORTING;
    constexpr auto T_TOTAL_EXECUTION = npb::is::ISParameters::T_TOTAL_EXECUTION;
}

namespace npb {
namespace is {

// Update the load_parameters implementation
ISParameters load_parameters(char class_id) {
    ISParameters params;
    
    // Set class ID from parameter
    params.class_id = class_id;
    
    // Set parameters based on class
    switch (class_id) {
        case 'S':
            params.total_keys = 1 << 16;          // 65,536
            params.max_key = 1 << 16;             // 65,536 
            params.iterations = 10;
            params.num_buckets = 1 << 10;         // 1,024
            break;
        case 'W':
            params.total_keys = 1 << 20;          // 1,048,576
            params.max_key = 1 << 20;             // 1,048,576
            params.iterations = 10;
            params.num_buckets = 1 << 10;         // 1,024
            break;
        case 'A':
            params.total_keys = 1 << 23;          // 8,388,608
            params.max_key = 1 << 23;             // 8,388,608
            params.iterations = 10;
            params.num_buckets = 1 << 10;         // 1,024
            break;
        case 'B':
            params.total_keys = 1 << 25;          // 33,554,432
            params.max_key = 1 << 25;             // 33,554,432
            params.iterations = 10;
            params.num_buckets = 1 << 10;         // 1,024
            break;
        case 'C':
            params.total_keys = 1 << 27;          // 134,217,728
            params.max_key = 1 << 27;             // 134,217,728
            params.iterations = 10;
            params.num_buckets = 1 << 10;         // 1,024
            break;
        case 'D':
            params.total_keys = 1 << 31;          // 2,147,483,648
            params.max_key = 1 << 31;             // 2,147,483,648
            params.iterations = 10;
            params.num_buckets = 1 << 10;         // 1,024
            break;
        default:
            std::cerr << "ERROR: Unknown class '" << class_id << "'. Using class S.\n";
            return load_parameters('S');
    }
    
    // Set up verification values based on class
    switch (params.class_id) {
        case 'S':
            params.test_index_array = {48427, 17148, 23627, 62548, 4431};
            params.test_rank_array = {0, 18, 346, 64917, 65463};
            break;
        case 'W':
            params.test_index_array = {357773, 934767, 875723, 898999, 404505};
            params.test_rank_array = {1249, 11698, 1039987, 1043896, 1048018};
            break;
        case 'A':
            params.test_index_array = {2112377, 662041, 5336171, 3642833, 4250760};
            params.test_rank_array = {104, 17523, 123928, 8288932, 8388264};
            break;
        case 'B':
            params.test_index_array = {41869, 812306, 5102857, 18232239, 26860214};
            params.test_rank_array = {33422937, 10244, 59149, 33135281, 99};
            break;
        case 'C':
            params.test_index_array = {44172927, 72999161, 74326391, 129606274, 21736814};
            params.test_rank_array = {61147, 882988, 266290, 133997595, 133525895};
            break;
        case 'D':
            params.test_index_array = {1317351170, 995930646, 1157283250, 1503301535, 1453734525};
            params.test_rank_array = {1, 36538729, 1978098519, 2145192618, 2147425337};
            break;
        default:
            // Default values for unknown class
            params.test_index_array = {0, 0, 0, 0, 0};
            params.test_rank_array = {0, 0, 0, 0, 0};
            break;
    }
    
    return params;
}

// IntegerSort constructor
IntegerSort::IntegerSort(const ISParameters& params) : params_(params) {
    // Allocate arrays
    key_array_.resize(params.total_keys);
    key_buff1_.resize(params.max_key);
    key_buff2_.resize(params.total_keys);
    partial_verify_vals_.resize(params.TEST_ARRAY_SIZE);
    
    // Initialize timers
    FILE* fp;
    if ((fp = fopen("timer.flag", "r")) != nullptr) {
        timers_enabled_ = true;
        fclose(fp);
    }
    
    timer_clear(T_BENCHMARKING);
    if (timers_enabled_) {
        timer_clear(T_INITIALIZATION);
        timer_clear(T_SORTING);
        timer_clear(T_TOTAL_EXECUTION);
    }
    
    // Allocate additional buffers
    allocate_key_buffer();
}

IntegerSort::~IntegerSort() = default;

// Timer methods
void IntegerSort::timer_clear(int timer) {
    timer_values_[timer] = 0.0;
}

void IntegerSort::timer_start(int timer) {
    timer_values_[timer] -= omp_get_wtime();
}

void IntegerSort::timer_stop(int timer) {
    timer_values_[timer] += omp_get_wtime();
}

double IntegerSort::timer_read(int timer) const {
    return timer_values_[timer];
}

// Memory allocation
void IntegerSort::allocate_key_buffer() {
    int num_procs = omp_get_max_threads();
    
    #ifdef USE_BUCKETS
    // Bucket sort memory allocation
    bucket_size_.resize(num_procs);
    for (int i = 0; i < num_procs; i++) {
        bucket_size_[i].resize(params_.num_buckets, 0);
    }
    
    bucket_ptrs_.resize(params_.num_buckets);
    
    // Initialize key_buff2
    std::fill(key_buff2_.begin(), key_buff2_.end(), 0);
    #else
    // Non-bucket sort allocation
    key_buff1_aptr_.resize(num_procs);
    key_buff1_aptr_[0] = key_buff1_.data();
    
    for (int i = 1; i < num_procs; i++) {
        key_buff1_aptr_[i] = static_cast<KeyType*>(alloc_memory(sizeof(KeyType) * params_.max_key));
    }
    #endif
}

void* IntegerSort::alloc_memory(size_t size) {
    void* p = std::malloc(size);
    if (!p) {
        std::cerr << "Memory allocation error" << std::endl;
        exit(1);
    }
    return p;
}

// Random number generator
double IntegerSort::randlc(double* x, double a) {
    constexpr double r23 = 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5;
    constexpr double r46 = r23 * r23;
    constexpr double t23 = 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0;
    constexpr double t46 = t23 * t23;
    
    double t1, t2, t3, t4, a1, a2, x1, x2, z;
    
    // Break A into two parts
    t1 = r23 * a;
    a1 = static_cast<int>(t1);
    a2 = a - t23 * a1;
    
    // Break X into two parts
    t1 = r23 * (*x);
    x1 = static_cast<int>(t1);
    x2 = (*x) - t23 * x1;
    
    // Compute Z = A1 * X2 + A2 * X1 (mod 2^23)
    t1 = a1 * x2 + a2 * x1;
    t2 = static_cast<int>(r23 * t1);
    z = t1 - t23 * t2;
    
    // Compute X = 2^23 * Z + A2 * X2 (mod 2^46)
    t3 = t23 * z + a2 * x2;
    t4 = static_cast<int>(r46 * t3);
    *x = t3 - t46 * t4;
    
    return (r46 * (*x));
}

void IntegerSort::vranlc(int n, double* x_seed, double a, double y[]) {
    constexpr double r23 = 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5;
    constexpr double r46 = r23 * r23;
    constexpr double t23 = 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0;
    constexpr double t46 = t23 * t23;
    
    double t1, t2, t3, t4, a1, a2, x1, x2, z;
    
    // Break A into two parts
    t1 = r23 * a;
    a1 = static_cast<int>(t1);
    a2 = a - t23 * a1;
    
    double x = *x_seed;
    
    // Generate N results
    for (int i = 0; i < n; i++) {
        // Break X into two parts
        t1 = r23 * x;
        x1 = static_cast<int>(t1);
        x2 = x - t23 * x1;
        
        // Compute Z = A1 * X2 + A2 * X1 (mod 2^23)
        t1 = a1 * x2 + a2 * x1;
        t2 = static_cast<int>(r23 * t1);
        z = t1 - t23 * t2;
        
        // Compute X = 2^23 * Z + A2 * X2 (mod 2^46)
        t3 = t23 * z + a2 * x2;
        t4 = static_cast<int>(r46 * t3);
        x = t3 - t46 * t4;
        
        y[i] = r46 * x;
    }
    
    *x_seed = x;
}

// Create sequence of random numbers
void IntegerSort::create_seq(double seed, double a) {
    #pragma omp parallel
    {
        double x, s;
        KeyType k;
        
        int thread_id = omp_get_thread_num();
        int num_threads = omp_get_num_threads();
        
        // Determine workload for this thread
        KeyType keys_per_thread = (params_.total_keys + num_threads - 1) / num_threads;
        KeyType start_key = keys_per_thread * thread_id;
        KeyType end_key = std::min(start_key + keys_per_thread, static_cast<KeyType>(params_.total_keys));
        
        // Find starting seed for this thread
        s = find_my_seed(thread_id, num_threads, 4 * params_.total_keys, seed, a);
        
        k = params_.max_key / 4;
        
        // Generate keys for this thread
        for (KeyType i = start_key; i < end_key; i++) {
            x = randlc(&s, a);
            x += randlc(&s, a);
            x += randlc(&s, a);
            x += randlc(&s, a);
            key_array_[i] = k * x;
        }
    }
}

// Find random seed for parallel random number generation
double IntegerSort::find_my_seed(int kn, int np, int64_t nn, double s, double a) {
    double t1, t2;
    int64_t mq, nq, kk, ik;
    
    if (kn == 0) return s;
    
    mq = (nn / 4 + np - 1) / np;
    nq = mq * 4 * kn;
    
    t1 = s;
    t2 = a;
    kk = nq;
    
    while (kk > 1) {
        ik = kk / 2;
        if (2 * ik == kk) {
            (void)randlc(&t2, t2);
            kk = ik;
        } else {
            (void)randlc(&t1, t2);
            kk = kk - 1;
        }
    }
    
    (void)randlc(&t1, t2);
    
    return t1;
}

// Main benchmark execution
void IntegerSort::run() {
    if (timers_enabled_) timer_start(T_TOTAL_EXECUTION);
    
    // Initialize test arrays
    for (int i = 0; i < params_.TEST_ARRAY_SIZE; i++) {
        partial_verify_vals_[i] = 0;
    }
    
    // Initialize random number generator and keys
    if (timers_enabled_) timer_start(T_INITIALIZATION);
    create_seq(314159265.0, 1220703125.0);
    if (timers_enabled_) timer_stop(T_INITIALIZATION);
    
    // Do one iteration to initialize data and code pages
    rank(1);
    
    // Reset verification counter
    passed_verification_ = 0;
    
    if (params_.class_id != 'S') {
        std::cout << "\n   iteration\n";
    }
    
    // Start benchmark timing
    timer_start(T_BENCHMARKING);
    
    // Main benchmark loop
    for (int it = 1; it <= params_.iterations; it++) {
        if (params_.class_id != 'S') {
            std::cout << "        " << it << "\n";
        }
        rank(it);
    }
    
    // End benchmark timing
    timer_stop(T_BENCHMARKING);
    
    // Perform final verification
    if (timers_enabled_) timer_start(T_SORTING);
    full_verify();
    if (timers_enabled_) timer_stop(T_SORTING);
    
    if (timers_enabled_) timer_stop(T_TOTAL_EXECUTION);
    
    // Store execution time
    execution_time_ = timer_read(T_BENCHMARKING);
    
    // Check verification
    verified_ = (passed_verification_ == 5 * params_.iterations + 1);
}

// Core ranking function
void IntegerSort::rank(int iteration) {
    const int shift = std::log2(params_.max_key) - std::log2(params_.num_buckets);
    const KeyType num_bucket_keys = 1L << shift;
    
    // Set special values for partial verification
    key_array_[iteration] = iteration;
    key_array_[iteration + params_.iterations] = params_.max_key - iteration;
    
    // Load partial verify values
    for (int i = 0; i < params_.TEST_ARRAY_SIZE; i++) {
        partial_verify_vals_[i] = key_array_[params_.test_index_array[i]];
    }
    
    // Setup pointers to key buffers
    #ifdef USE_BUCKETS
    KeyType* key_buff_ptr2 = key_buff2_.data();
    #else
    KeyType* key_buff_ptr2 = key_array_.data();
    #endif
    KeyType* key_buff_ptr = key_buff1_.data();
    
    // Clear key_buff1 (rank array)
    std::fill(key_buff_ptr, key_buff_ptr + params_.max_key, 0);
    
    #ifdef USE_BUCKETS
    // Perform bucket sort with enhanced synchronization
    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        int num_threads = omp_get_num_threads();
        auto& work_buff = bucket_size_[thread_id];
        
        // Initialize buckets
        std::fill(work_buff.begin(), work_buff.end(), 0);
        
        // Count keys per bucket for this thread
        #pragma omp for schedule(static)
        for (int i = 0; i < params_.total_keys; i++) {
            work_buff[key_array_[i] >> shift]++;
        }
        
        // Synchronize threads before bucket pointer calculation
        #pragma omp barrier
        
        // Calculate global bucket pointers with explicit synchronization
        #pragma omp single
        {
            // Initialize first bucket pointer
            bucket_ptrs_[0] = 0;
            
            // Calculate running sum of bucket sizes
            for (int i = 1; i < params_.num_buckets; i++) {
                bucket_ptrs_[i] = bucket_ptrs_[i-1];
                
                // Add counts from all threads for previous bucket
                for (int t = 0; t < num_threads; t++) {
                    bucket_ptrs_[i] += bucket_size_[t][i-1];
                }
            }
        }
        
        // Synchronize before using bucket pointers
        #pragma omp barrier
        
        // Create thread-local copy of bucket pointers
        std::vector<KeyType> local_bucket_ptrs = bucket_ptrs_;
        
        // Add thread offset to pointers
        for (int i = 0; i < thread_id; i++) {
            for (int j = 0; j < params_.num_buckets; j++) {
                local_bucket_ptrs[j] += bucket_size_[i][j];
            }
        }
        
        // Sort keys into buckets
        #pragma omp for schedule(static)
        for (int i = 0; i < params_.total_keys; i++) {
            KeyType key = key_array_[i];
            KeyType bucket = key >> shift;
            key_buff2_[local_bucket_ptrs[bucket]++] = key;
        }
        
        // Update global bucket pointers with final counts
        #pragma omp single
        {
            KeyType sum = 0;
            for (int i = 0; i < params_.num_buckets; i++) {
                KeyType current = 0;
                for (int t = 0; t < num_threads; t++) {
                    current += bucket_size_[t][i];
                }
                sum += current;
                bucket_ptrs_[i] = sum;
            }
        }
        
        // Sort keys within each bucket (rank array population)
        #pragma omp for schedule(dynamic)
        for (int i = 0; i < params_.num_buckets; i++) {
            // Determine range for this bucket
            KeyType k1 = i * num_bucket_keys;
            KeyType k2 = k1 + num_bucket_keys;
            
            // Clear the rank array for this bucket
            for (KeyType k = k1; k < k2; k++) {
                key_buff_ptr[k] = 0;
            }
            
            // Count occurrence of each key in this bucket
            KeyType bucket_start = (i > 0) ? bucket_ptrs_[i-1] : 0;
            KeyType bucket_end = bucket_ptrs_[i];
            
            for (KeyType k = bucket_start; k < bucket_end; k++) {
                key_buff_ptr[key_buff_ptr2[k]]++;
            }
            
            // Calculate ranks by cumulative sum
            KeyType sum = bucket_start;
            for (KeyType k = k1; k < k2; k++) {
                sum += key_buff_ptr[k];
                key_buff_ptr[k] = sum;
            }
        }
    }
    #else
    // Non-bucket sort with better synchronization
    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        KeyType* work_buff = key_buff1_aptr_[thread_id];
        
        // Clear work array
        std::fill(work_buff, work_buff + params_.max_key, 0);
        
        // Count occurrences of each key
        #pragma omp for schedule(static)
        for (int i = 0; i < params_.total_keys; i++) {
            work_buff[key_buff_ptr2[i]]++;
        }
        
        // Merge counts from all threads with proper synchronization
        #pragma omp critical
        {
            for (KeyType i = 0; i < params_.max_key; i++) {
                key_buff_ptr[i] += work_buff[i];
            }
        }
        
        // Calculate ranks by cumulative sum
        #pragma omp barrier
        #pragma omp single
        {
            KeyType sum = 0;
            for (KeyType i = 0; i < params_.max_key; i++) {
                sum += key_buff_ptr[i];
                key_buff_ptr[i] = sum;
            }
        }
    }
    #endif

    // Perform partial verification tests
    for (int i = 0; i < params_.TEST_ARRAY_SIZE; i++) {
        KeyType k = partial_verify_vals_[i];
        if (0 < k && k <= static_cast<KeyType>(params_.total_keys - 1)) {
            KeyType key_rank = key_buff_ptr[k-1];
            int failed = 0;
            
            switch (params_.class_id) {
                case 'S':
                    if (i <= 2) {
                        if (key_rank != params_.test_rank_array[i] + iteration) {
                            failed = 1;
                        } else {
                            passed_verification_++;
                        }
                    } else {
                        if (key_rank != params_.test_rank_array[i] - iteration) {
                            failed = 1;
                        } else {
                            passed_verification_++;
                        }
                    }
                    break;
                case 'W':
                    if (i < 2) {
                        if (key_rank != params_.test_rank_array[i] + (iteration-2)) {
                            failed = 1;
                        } else {
                            passed_verification_++;
                        }
                    } else {
                        if (key_rank != params_.test_rank_array[i] - iteration) {
                            failed = 1;
                        } else {
                            passed_verification_++;
                        }
                    }
                    break;
                case 'A':
                    if (i <= 2) {
                        if (key_rank != params_.test_rank_array[i] + (iteration-1)) {
                            failed = 1;
                        } else {
                            passed_verification_++;
                        }
                    } else {
                        if (key_rank != params_.test_rank_array[i] - (iteration-1)) {
                            failed = 1;
                        } else {
                            passed_verification_++;
                        }
                    }
                    break;
                case 'B':
                    if (i == 1 || i == 2 || i == 4) {
                        if (key_rank != params_.test_rank_array[i] + iteration) {
                            failed = 1;
                        } else {
                            passed_verification_++;
                        }
                    } else {
                        if (key_rank != params_.test_rank_array[i] - iteration) {
                            failed = 1;
                        } else {
                            passed_verification_++;
                        }
                    }
                    break;
                case 'C':
                    if (i <= 2) {
                        if (key_rank != params_.test_rank_array[i] + iteration) {
                            failed = 1;
                        } else {
                            passed_verification_++;
                        }
                    } else {
                        if (key_rank != params_.test_rank_array[i] - iteration) {
                            failed = 1;
                        } else {
                            passed_verification_++;
                        }
                    }
                    break;
                case 'D':
                    if (i < 2) {
                        if (key_rank != params_.test_rank_array[i] + iteration) {
                            failed = 1;
                        } else {
                            passed_verification_++;
                        }
                    } else {
                        if (key_rank != params_.test_rank_array[i] - iteration) {
                            failed = 1;
                        } else {
                            passed_verification_++;
                        }
                    }
                    break;
            }
            
            if (failed == 1) {
                std::cout << "Failed partial verification: iteration " << iteration 
                        << ", test key " << i << std::endl;
            }
        }
    }
    
    // Save key_buff_ptr for full verification
    if (iteration == params_.iterations) {
        key_buff_ptr_global_ = key_buff_ptr;
    }
}

// Final full verification
void IntegerSort::full_verify() {
    // Now sort the keys and check if they are correctly sorted
    #ifdef USE_BUCKETS
    // Buckets are already sorted. Just need to sort within each bucket
    
    #pragma omp parallel for schedule(dynamic)
    for (int j = 0; j < params_.num_buckets; j++) {
        KeyType k1 = (j > 0) ? bucket_ptrs_[j-1] : 0;
        
        for (KeyType i = k1; i < bucket_ptrs_[j]; i++) {
            KeyType k = --key_buff_ptr_global_[key_buff2_[i]];
            key_array_[k] = key_buff2_[i];
        }
    }
    #else
   // Copy keys for sorting
   std::copy(key_array_.begin(), key_array_.end(), key_buff2_.begin());
   
   // This is the actual sorting. Each thread handles a subset of key values
   int num_threads = omp_get_max_threads();
   KeyType j = (params_.max_key + num_threads - 1) / num_threads;
   
   #pragma omp parallel
   {
       int thread_id = omp_get_thread_num();
       KeyType start_key = j * thread_id;
       KeyType end_key = std::min(start_key + j, static_cast<KeyType>(params_.max_key));
       
       for (KeyType i = 0; i < params_.total_keys; i++) {
           if (key_buff2_[i] >= start_key && key_buff2_[i] < end_key) {
               KeyType k = --key_buff_ptr_global_[key_buff2_[i]];
               key_array_[k] = key_buff2_[i];
           }
       }
   }
   #endif
   
   // Verify that keys are in ascending order
   int error_count = 0;
   
   #pragma omp parallel for reduction(+:error_count)
   for (int i = 1; i < params_.total_keys; i++) {
       if (key_array_[i-1] > key_array_[i]) {
           error_count++;
       }
   }
   
   if (error_count != 0) {
       std::cout << "Full_verify: number of keys out of sort: " << error_count << std::endl;
   } else {
       passed_verification_++;
   }
}

// Get benchmark results
double IntegerSort::getExecutionTime() const {
   return execution_time_;
}

double IntegerSort::getMopsTotal() const {
   return static_cast<double>(params_.iterations * params_.total_keys) / execution_time_ / 1000000.0;
}

bool IntegerSort::getVerificationStatus() const {
   return verified_;
}

// Print benchmark results
void print_results(const IntegerSort& is, const ISParameters& params, 
                 const std::string& name, const std::string& optype) {
   double mops = is.getMopsTotal();
   double t = is.getExecutionTime();
   bool verified = is.getVerificationStatus();
   
   std::cout << "\n\n IS Benchmark Completed\n";
   std::cout << " Class          =                        " << params.class_id << "\n";
   std::cout << " Size           =             " << std::setw(12) << params.total_keys << "\n";
   std::cout << " Iterations     =             " << std::setw(12) << params.iterations << "\n";
   std::cout << " Time in seconds =            " << std::setw(12) << std::fixed << std::setprecision(2) << t << "\n";
   std::cout << " Mop/s total    =             " << std::setw(12) << std::fixed << std::setprecision(2) << mops << "\n";
   std::cout << " Operation type = " << std::setw(24) << optype << "\n";
   
   if (verified) {
       std::cout << " Verification    =               SUCCESSFUL\n";
   } else {
       std::cout << " Verification    =             UNSUCCESSFUL\n";
   }
   
   // Additional information about the build and environment
   std::cout << " Version         =             " << std::setw(12) << "4.1\n";
   
   // Get compiler information
   #ifdef __GNUG__
   std::string compiler_version = std::to_string(__GNUG__) + "." + 
                                  std::to_string(__GNUC_MINOR__) + "." + 
                                  std::to_string(__GNUC_PATCHLEVEL__);
   #elif defined(__clang__)
   std::string compiler_version = std::to_string(__clang_major__) + "." + 
                                  std::to_string(__clang_minor__) + "." + 
                                  std::to_string(__clang_patchlevel__);
   #else
   std::string compiler_version = "unknown";
   #endif
   
   // Get timestamp
   auto now = std::chrono::system_clock::now();
   auto time_t_now = std::chrono::system_clock::to_time_t(now);
   char buffer[64];
   std::strftime(buffer, sizeof(buffer), "%d %b %Y", std::localtime(&time_t_now));
   
   std::cout << " Compiler ver    =             " << std::setw(12) << compiler_version << "\n";
   std::cout << " OpenMP version  =             " << std::setw(12) << _OPENMP << "\n";
   std::cout << " Compile date    =             " << std::setw(12) << buffer << "\n\n";
   
   // Thread information
   std::cout << " Total threads   =             " << std::setw(12) << omp_get_max_threads() << "\n\n";
}

} // namespace is
} // namespace npb