#include "is.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <omp.h>
#include <cstring>
#include <memory>

#ifndef USE_BUCKETS
#define USE_BUCKETS
#endif

namespace {
    constexpr auto T_BENCHMARKING = npb::is::ISParameters::T_BENCHMARKING;
    constexpr auto T_INITIALIZATION = npb::is::ISParameters::T_INITIALIZATION;
    constexpr auto T_SORTING = npb::is::ISParameters::T_SORTING;
    constexpr auto T_TOTAL_EXECUTION = npb::is::ISParameters::T_TOTAL_EXECUTION;
}

namespace npb {
namespace is {

ISParameters load_parameters(char class_id) {
    ISParameters params;
    params.class_id = class_id;
    
    switch (class_id) {
        case 'S':
            params.total_keys = 1 << 16;
            params.max_key = 1 << 11;
            params.iterations = 10;
            params.num_buckets = 1 << 9;
            break;
        case 'W':
            params.total_keys = 1 << 20;
            params.max_key = 1 << 16;
            params.iterations = 10;
            params.num_buckets = 1 << 10;
            break;
        case 'A':
            params.total_keys = 1 << 23;
            params.max_key = 1 << 19;
            params.iterations = 10;
            params.num_buckets = 1 << 10;
            break;
        case 'B':
            params.total_keys = 1 << 25;
            params.max_key = 1 << 21;
            params.iterations = 10;
            params.num_buckets = 1 << 10;
            break;
        case 'C':
            params.total_keys = 1 << 27;
            params.max_key = 1 << 23;
            params.iterations = 10;
            params.num_buckets = 1 << 10;
            break;
        case 'D':
            params.total_keys = 1L << 31;
            params.max_key = 1L << 27;
            params.iterations = 10;
            params.num_buckets = 1 << 10;
            break;
        default:
            std::cerr << "ERROR: Unknown class '" << class_id << "'. Using class S.\n";
            return load_parameters('S');
    }
    
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
            params.test_index_array = {0, 0, 0, 0, 0};
            params.test_rank_array = {0, 0, 0, 0, 0};
            break;
    }
    
    return params;
}

IntegerSort::IntegerSort(const ISParameters& params) : params_(params) {
    key_array_.resize(params.total_keys);
    key_buff1_.resize(params.max_key);
    key_buff2_.resize(params.total_keys);
    partial_verify_vals_.resize(params.TEST_ARRAY_SIZE);
    
    FILE* fp;
    if ((fp = fopen("timer.flag", "r")) != nullptr) {
        timers_enabled_ = true;
        fclose(fp);
    }
    
    std::fill(timer_values_.begin(), timer_values_.end(), 0.0);
    allocate_key_buffer();
}

IntegerSort::~IntegerSort() {
    #ifndef USE_BUCKETS
    int num_procs = omp_get_max_threads();
    for (int i = 1; i < num_procs; i++) {
        if (key_buff1_aptr_[i]) {
            std::free(key_buff1_aptr_[i]);
        }
    }
    #endif
}

void IntegerSort::timer_clear(int timer) {
    if (timers_enabled_) {
        timer_values_[timer] = 0.0;
    }
}

void IntegerSort::timer_start(int timer) {
    if (timers_enabled_) {
        timer_values_[timer] = omp_get_wtime();
    }
}

void IntegerSort::timer_stop(int timer) {
    if (timers_enabled_) {
        timer_values_[timer] = omp_get_wtime() - timer_values_[timer];
    }
}

double IntegerSort::timer_read(int timer) const {
    return timer_values_[timer];
}

void IntegerSort::allocate_key_buffer() {
    int num_procs = omp_get_max_threads();
    
    #ifdef USE_BUCKETS
    bucket_size_.resize(num_procs);
    for (int i = 0; i < num_procs; i++) {
        bucket_size_[i].resize(params_.num_buckets, 0);
    }
    bucket_ptrs_.resize(params_.num_buckets);
    std::fill(key_buff2_.begin(), key_buff2_.end(), 0);
    #else
    key_buff1_aptr_.resize(num_procs);
    key_buff1_aptr_[0] = key_buff1_.data();
    
    for (int i = 1; i < num_procs; i++) {
        key_buff1_aptr_[i] = static_cast<KeyType*>(
            std::aligned_alloc(64, sizeof(KeyType) * params_.max_key));
        if (!key_buff1_aptr_[i]) {
            throw std::bad_alloc();
        }
    }
    #endif
}

double IntegerSort::randlc(double* x, double a) {
    constexpr double r23 = 1.1920928955078125e-07;
    constexpr double r46 = 1.4210854715202004e-14;
    constexpr double t23 = 8388608.0;
    constexpr double t46 = 7.0368744177664e+13;
    
    double t1, t2, t3, t4, a1, a2, x1, x2, z;
    
    t1 = r23 * a;
    a1 = static_cast<int>(t1);
    a2 = a - t23 * a1;
    
    t1 = r23 * (*x);
    x1 = static_cast<int>(t1);
    x2 = (*x) - t23 * x1;
    
    t1 = a1 * x2 + a2 * x1;
    t2 = static_cast<int>(r23 * t1);
    z = t1 - t23 * t2;
    
    t3 = t23 * z + a2 * x2;
    t4 = static_cast<int>(r46 * t3);
    *x = t3 - t46 * t4;
    
    return (r46 * (*x));
}

void IntegerSort::create_seq(double seed, double a) {
    #pragma omp parallel
    {
        double x, s;
        KeyType k;
        
        int thread_id = omp_get_thread_num();
        int num_threads = omp_get_num_threads();
        
        KeyType keys_per_thread = (params_.total_keys + num_threads - 1) / num_threads;
        KeyType start_key = keys_per_thread * thread_id;
        KeyType end_key = std::min(start_key + keys_per_thread, 
                                   static_cast<KeyType>(params_.total_keys));
        
        s = find_my_seed(thread_id, num_threads, 
                        static_cast<int64_t>(4) * params_.total_keys, seed, a);
        
        k = params_.max_key / 4;
        
        for (KeyType i = start_key; i < end_key; i++) {
            x = randlc(&s, a);
            x += randlc(&s, a);
            x += randlc(&s, a);
            x += randlc(&s, a);
            key_array_[i] = k * x;
        }
    }
}

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

void IntegerSort::run() {
    if (timers_enabled_) timer_start(T_TOTAL_EXECUTION);
    
    if (timers_enabled_) timer_start(T_INITIALIZATION);
    create_seq(314159265.0, 1220703125.0);
    if (timers_enabled_) timer_stop(T_INITIALIZATION);
    
    rank(1);
    passed_verification_ = 0;
    
    if (params_.class_id != 'S') {
        std::cout << "\n   iteration\n";
    }
    
    // Bezpośredni pomiar czasu dla głównej pętli benchmarku
    double start_time = omp_get_wtime();
    
    for (int it = 1; it <= params_.iterations; it++) {
        if (params_.class_id != 'S') {
            std::cout << "        " << it << "\n";
        }
        rank(it);
    }
    
    double end_time = omp_get_wtime();
    execution_time_ = end_time - start_time;
    
    if (timers_enabled_) timer_start(T_SORTING);
    full_verify();
    if (timers_enabled_) timer_stop(T_SORTING);
    
    if (timers_enabled_) timer_stop(T_TOTAL_EXECUTION);
    
    verified_ = (passed_verification_ == 5 * params_.iterations + 1);
}

void IntegerSort::rank(int iteration) {
    KeyType* key_buff_ptr2;
    KeyType* key_buff_ptr;
    
    key_array_[iteration] = iteration;
    key_array_[iteration + params_.iterations] = params_.max_key - iteration;
    
    for (int i = 0; i < params_.TEST_ARRAY_SIZE; i++) {
        partial_verify_vals_[i] = key_array_[params_.test_index_array[i]];
    }
    
    #ifdef USE_BUCKETS
    key_buff_ptr2 = key_buff2_.data();
    #else
    key_buff_ptr2 = key_array_.data();
    #endif
    key_buff_ptr = key_buff1_.data();
    
    #ifdef USE_BUCKETS
    const int shift = static_cast<int>(std::log2(params_.max_key)) - 
                      static_cast<int>(std::log2(params_.num_buckets));
    const KeyType num_bucket_keys = 1L << shift;
    
    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        int num_threads = omp_get_num_threads();
        auto& work_buff = bucket_size_[thread_id];
        
        std::fill(work_buff.begin(), work_buff.end(), 0);
        
        #pragma omp for schedule(static)
        for (int i = 0; i < params_.total_keys; i++) {
            work_buff[key_array_[i] >> shift]++;
        }
        
        #pragma omp barrier
        
        #pragma omp single
        {
            bucket_ptrs_[0] = 0;
            for (int i = 1; i < params_.num_buckets; i++) {
                bucket_ptrs_[i] = bucket_ptrs_[i-1];
                for (int k = 0; k < num_threads; k++) {
                    bucket_ptrs_[i] += bucket_size_[k][i-1];
                }
            }
        }
        
        #pragma omp barrier
        
        std::vector<KeyType> my_bucket_ptrs(params_.num_buckets);
        for (int i = 0; i < params_.num_buckets; i++) {
            my_bucket_ptrs[i] = bucket_ptrs_[i];
            for (int k = 0; k < thread_id; k++) {
                my_bucket_ptrs[i] += bucket_size_[k][i];
            }
        }
        
        #pragma omp for schedule(static)
        for (int i = 0; i < params_.total_keys; i++) {
            KeyType k = key_array_[i];
            key_buff2_[my_bucket_ptrs[k >> shift]++] = k;
        }
        
        #pragma omp single
        {
            for (int i = 0; i < params_.num_buckets; i++) {
                bucket_ptrs_[i] = 0;
                for (int k = 0; k < num_threads; k++) {
                    bucket_ptrs_[i] += bucket_size_[k][i];
                }
                if (i > 0) {
                    bucket_ptrs_[i] += bucket_ptrs_[i-1];
                }
            }
        }
        
        #pragma omp for schedule(dynamic)
        for (int i = 0; i < params_.num_buckets; i++) {
            KeyType k1 = i * num_bucket_keys;
            KeyType k2 = k1 + num_bucket_keys;
            
            for (KeyType k = k1; k < k2; k++) {
                key_buff_ptr[k] = 0;
            }
            
            KeyType m = (i > 0) ? bucket_ptrs_[i-1] : 0;
            for (KeyType k = m; k < bucket_ptrs_[i]; k++) {
                key_buff_ptr[key_buff_ptr2[k]]++;
            }
            
            key_buff_ptr[k1] += m;
            for (KeyType k = k1 + 1; k < k2; k++) {
                key_buff_ptr[k] += key_buff_ptr[k-1];
            }
        }
    }
    #else
    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        int num_threads = omp_get_num_threads();
        KeyType* work_buff = key_buff1_aptr_[thread_id];
        
        std::fill(work_buff, work_buff + params_.max_key, 0);
        
        #pragma omp for schedule(static)
        for (int i = 0; i < params_.total_keys; i++) {
            work_buff[key_buff_ptr2[i]]++;
        }
        
        #pragma omp barrier
        
        #pragma omp critical
        {
            for (KeyType i = 0; i < params_.max_key; i++) {
                key_buff_ptr[i] += work_buff[i];
            }
        }
        
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
    
    if (iteration == params_.iterations) {
        key_buff_ptr_global_ = key_buff_ptr;
    }
}

void IntegerSort::full_verify() {
    #ifdef USE_BUCKETS
    #pragma omp parallel for schedule(dynamic)
    for (int j = 0; j < params_.num_buckets; j++) {
        KeyType k1 = (j > 0) ? bucket_ptrs_[j-1] : 0;
        
        for (KeyType i = k1; i < bucket_ptrs_[j]; i++) {
            KeyType k = --key_buff_ptr_global_[key_buff2_[i]];
            key_array_[k] = key_buff2_[i];
        }
    }
    #else
    for (KeyType i = 0; i < params_.total_keys; i++) {
        key_buff2_[i] = key_array_[i];
    }
    
    int num_threads = omp_get_max_threads();
    KeyType j = (params_.max_key + num_threads - 1) / num_threads;
    
    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        KeyType start_key = j * thread_id;
        KeyType end_key = std::min(start_key + j, 
                                   static_cast<KeyType>(params_.max_key));
        
        for (KeyType i = 0; i < params_.total_keys; i++) {
            if (key_buff2_[i] >= start_key && key_buff2_[i] < end_key) {
                KeyType k = --key_buff_ptr_global_[key_buff2_[i]];
                key_array_[k] = key_buff2_[i];
            }
        }
    }
    #endif
    
    KeyType error_count = 0;
    
    #pragma omp parallel for reduction(+:error_count)
    for (int i = 1; i < params_.total_keys; i++) {
        if (key_array_[i-1] > key_array_[i]) {
            error_count++;
        }
    }
    
    if (error_count != 0) {
        std::cout << "Full_verify: number of keys out of sort: " 
                  << error_count << std::endl;
    } else {
        passed_verification_++;
    }
}

double IntegerSort::getExecutionTime() const {
    return execution_time_;
}

double IntegerSort::getMopsTotal() const {
    return static_cast<double>(params_.iterations * params_.total_keys) / 
           execution_time_ / 1000000.0;
}

bool IntegerSort::getVerificationStatus() const {
    return verified_;
}

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
    
    std::cout << " Version         =             " << std::setw(12) << "4.1\n";
    std::cout << " Total threads   =             " << std::setw(12) << omp_get_max_threads() << "\n\n";
}

}
}