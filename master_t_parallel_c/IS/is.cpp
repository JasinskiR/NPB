#include "is.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <omp.h>
#include <cstring>
#include <memory>

namespace npb {
namespace is {

template<std::integral KeyType>
ISParameters<KeyType> load_parameters(char class_id) {
    ISParameters<KeyType> params{.class_id = class_id};
    
    switch (class_id) {
        case 'S':
            params.total_keys = 1L << 16;
            params.max_key = 1 << 11;
            params.num_buckets = 1 << 9;
            params.iterations = 10;
            params.test_index_array = {48427, 17148, 23627, 62548, 4431};
            params.test_rank_array = {0, 18, 346, 64917, 65463};
            break;
        case 'W':
            params.total_keys = 1L << 20;
            params.max_key = 1 << 16;
            params.num_buckets = 1 << 10;
            params.iterations = 10;
            params.test_index_array = {357773, 934767, 875723, 898999, 404505};
            params.test_rank_array = {1249, 11698, 1039987, 1043896, 1048018};
            break;
        case 'A':
            params.total_keys = 1L << 23;
            params.max_key = 1 << 19;
            params.num_buckets = 1 << 10;
            params.iterations = 10;
            params.test_index_array = {2112377, 662041, 5336171, 3642833, 4250760};
            params.test_rank_array = {104, 17523, 123928, 8288932, 8388264};
            break;
        case 'B':
            params.total_keys = 1L << 25;
            params.max_key = 1 << 21;
            params.num_buckets = 1 << 10;
            params.iterations = 10;
            params.test_index_array = {41869, 812306, 5102857, 18232239, 26860214};
            params.test_rank_array = {33422937, 10244, 59149, 33135281, 99};
            break;
        case 'C':
            params.total_keys = 1L << 27;
            params.max_key = 1 << 23;
            params.num_buckets = 1 << 10;
            params.iterations = 10;
            params.test_index_array = {44172927, 72999161, 74326391, 129606274, 21736814};
            params.test_rank_array = {61147, 882988, 266290, 133997595, 133525895};
            break;
        case 'D':
            params.total_keys = 1L << 31;
            params.max_key = 1L << 27;
            params.num_buckets = 1 << 10;
            params.iterations = 10;
            params.test_index_array = {1317351170, 995930646, 1157283250, 1503301535, 1453734525};
            params.test_rank_array = {1, 36538729, 1978098519, 2145192618, 2147425337};
            break;
        default:
            std::cerr << "ERROR: Unknown class '" << class_id << "'. Using class S.\n";
            return load_parameters<KeyType>('S');
    }
    
    return params;
}

template<std::integral KeyType>
IntegerSort<KeyType>::TimerGuard::TimerGuard(IntegerSort& is, int timer_id) 
    : is_(is), timer_id_(timer_id) {
    if (is_.timers_enabled_) {
        is_.timer_values_[timer_id_] = omp_get_wtime();
    }
}

template<std::integral KeyType>
IntegerSort<KeyType>::TimerGuard::~TimerGuard() {
    if (is_.timers_enabled_) {
        is_.timer_values_[timer_id_] = omp_get_wtime() - is_.timer_values_[timer_id_];
    }
}

template<std::integral KeyType>
IntegerSort<KeyType>::IntegerSort(const ISParameters<KeyType>& params) 
    : params_(params), 
      key_array_(params.total_keys),
      key_buff1_(params.max_key),
      key_buff2_(params.total_keys),
      partial_verify_vals_(params.TEST_ARRAY_SIZE) {
    
    timers_enabled_ = std::filesystem::exists("timer.flag");
    timer_values_.fill(0.0);
    allocate_key_buffer();
}

template<std::integral KeyType>
void IntegerSort<KeyType>::allocate_key_buffer() {
    const int num_procs = omp_get_max_threads();
    
    if constexpr (use_buckets) {
        bucket_size_.resize(num_procs);
        for (auto& bucket : bucket_size_) {
            bucket.resize(params_.num_buckets, 0);
        }
        bucket_ptrs_.resize(params_.num_buckets);
        
        std::fill(key_buff2_.begin(), key_buff2_.end(), 0);
    } else {
        key_buff1_aptr_.resize(num_procs);
        key_buff1_aptr_[0] = key_buff1_.data();
        
        for (int i = 1; i < num_procs; ++i) {
            key_buff1_aptr_[i] = static_cast<KeyType*>(
                std::aligned_alloc(64, sizeof(KeyType) * params_.max_key));
            if (!key_buff1_aptr_[i]) {
                throw std::bad_alloc{};
            }
        }
    }
}

template<std::integral KeyType>
auto IntegerSort<KeyType>::make_timer_guard(int timer_id) {
    return TimerGuard{*this, timer_id};
}

template<std::integral KeyType>
void IntegerSort<KeyType>::run() {
    auto timer_guard = make_timer_guard(params_.T_TOTAL_EXECUTION);
    
    {
        auto init_timer = make_timer_guard(params_.T_INITIALIZATION);
        create_seq(314159265.0, 1220703125.0);
    }
    
    rank(1);
    passed_verification_ = 0;
    
    if (params_.class_id != 'S') {
        std::cout << "\n   iteration\n";
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int it = 1; it <= params_.iterations; ++it) {
        if (params_.class_id != 'S') {
            std::cout << "        " << it << "\n";
        }
        rank(it);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    execution_time_ = std::chrono::duration<double>(end_time - start_time).count();
    
    {
        auto sort_timer = make_timer_guard(params_.T_SORTING);
        full_verify();
    }
    
    verified_ = (passed_verification_ == 5 * params_.iterations + 1);
}

static inline double randlc(double* x, double a) {
    static constexpr double r23 = 1.1920928955078125e-07;
    static constexpr double r46 = 1.4210854715202004e-14;
    static constexpr double t23 = 8388608.0;
    static constexpr double t46 = 7.0368744177664e+13;
    
    const double t1 = r23 * a;
    const int a1 = static_cast<int>(t1);
    const double a2 = a - t23 * a1;
    
    const double tx = r23 * (*x);
    const int x1 = static_cast<int>(tx);
    const double x2 = (*x) - t23 * x1;
    
    const double t1_new = a1 * x2 + a2 * x1;
    const int t2 = static_cast<int>(r23 * t1_new);
    const double z = t1_new - t23 * t2;
    
    const double t3 = t23 * z + a2 * x2;
    const int t4 = static_cast<int>(r46 * t3);
    *x = t3 - t46 * t4;
    
    return r46 * (*x);
}

template<std::integral KeyType>
double IntegerSort<KeyType>::find_my_seed(int kn, int np, int64_t nn, double s, double a) {
    if (kn == 0) return s;
    
    const int64_t mq = (nn / 4 + np - 1) / np;
    const int64_t nq = mq * 4 * kn;
    
    double t1 = s;
    double t2 = a;
    int64_t kk = nq;
    
    while (kk > 1) {
        const int64_t ik = kk / 2;
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

template<std::integral KeyType>
void IntegerSort<KeyType>::create_seq(double seed, double a) {
    const KeyType k = params_.max_key / 4;
    
    #pragma omp parallel
    {
        const int thread_id = omp_get_thread_num();
        const int num_threads = omp_get_num_threads();
        
        const KeyType keys_per_thread = (params_.total_keys + num_threads - 1) / num_threads;
        const KeyType start_key = keys_per_thread * thread_id;
        const KeyType end_key = std::min(start_key + keys_per_thread, 
                                        static_cast<KeyType>(params_.total_keys));
        
        double s = find_my_seed(thread_id, num_threads, 
                               static_cast<int64_t>(4) * params_.total_keys, seed, a);
        
        for (KeyType i = start_key; i < end_key; ++i) {
            double x = randlc(&s, a);
            x += randlc(&s, a);
            x += randlc(&s, a);
            x += randlc(&s, a);
            key_array_[i] = static_cast<KeyType>(k * x);
        }
    }
}

template<std::integral KeyType>
void IntegerSort<KeyType>::rank(int iteration) {
    key_array_[iteration] = iteration;
    key_array_[iteration + params_.iterations] = params_.max_key - iteration;
    
    for (int i = 0; i < params_.TEST_ARRAY_SIZE; i++) {
        partial_verify_vals_[i] = key_array_[params_.test_index_array[i]];
    }
    
    if constexpr (use_buckets) {
        rank_with_buckets(iteration);
    } else {
        rank_without_buckets(iteration);
    }
    
    verify_partial_results(iteration);
    
    if (iteration == params_.iterations) {
        key_buff_ptr_global_ = key_buff1_.data();
    }
}

template<std::integral KeyType>
void IntegerSort<KeyType>::rank_with_buckets(int iteration) {
    const int max_key_log2 = 32 - __builtin_clz(params_.max_key - 1);
    const int bucket_log2 = 32 - __builtin_clz(params_.num_buckets - 1);
    const int shift = max_key_log2 - bucket_log2;
    const KeyType num_bucket_keys = KeyType{1} << shift;
    
    #pragma omp parallel
    {
        const int thread_id = omp_get_thread_num();
        const int num_threads = omp_get_num_threads();
        KeyType* work_buff = bucket_size_[thread_id].data();
        
        for (int i = 0; i < params_.num_buckets; ++i) {
            work_buff[i] = 0;
        }
        
        #pragma omp for schedule(static) nowait
        for (int64_t i = 0; i < params_.total_keys; ++i) {
            const KeyType bucket_idx = key_array_[i] >> shift;
            if (bucket_idx < static_cast<KeyType>(params_.num_buckets)) {
                ++work_buff[bucket_idx];
            }
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
        
        std::vector<KeyType> my_bucket_start(params_.num_buckets);
        for (int i = 0; i < params_.num_buckets; i++) {
            my_bucket_start[i] = bucket_ptrs_[i];
            for (int k = 0; k < thread_id; k++) {
                my_bucket_start[i] += bucket_size_[k][i];
            }
        }
        
        #pragma omp for schedule(static) nowait
        for (int64_t i = 0; i < params_.total_keys; ++i) {
            const KeyType k = key_array_[i];
            const KeyType bucket_idx = k >> shift;
            if (bucket_idx < static_cast<KeyType>(params_.num_buckets)) {
                key_buff2_[my_bucket_start[bucket_idx]++] = k;
            }
        }
        
        #pragma omp barrier
        
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
        for (int i = 0; i < params_.num_buckets; ++i) {
            const KeyType k1 = i * num_bucket_keys;
            const KeyType k2 = std::min(k1 + num_bucket_keys, static_cast<KeyType>(params_.max_key));
            
            KeyType* key_buff_ptr = key_buff1_.data();
            
            for (KeyType k = k1; k < k2; k++) {
                key_buff_ptr[k] = 0;
            }
            
            const KeyType m = (i > 0) ? bucket_ptrs_[i-1] : 0;
            for (KeyType k = m; k < bucket_ptrs_[i]; k++) {
                const KeyType key_val = key_buff2_[k];
                if (key_val < static_cast<KeyType>(params_.max_key)) {
                    ++key_buff_ptr[key_val];
                }
            }
            
            key_buff_ptr[k1] += m;
            for (KeyType k = k1 + 1; k < k2; k++) {
                key_buff_ptr[k] += key_buff_ptr[k-1];
            }
        }
    }
}

template<std::integral KeyType>
void IntegerSort<KeyType>::rank_without_buckets(int iteration) {
    #pragma omp parallel
    {
        const int thread_id = omp_get_thread_num();
        const int num_threads = omp_get_num_threads();
        KeyType* work_buff = key_buff1_aptr_[thread_id];
        
        for (int k = 0; k < params_.max_key; ++k) {
            work_buff[k] = 0;
        }
        
        #pragma omp for schedule(static) nowait
        for (int64_t i = 0; i < params_.total_keys; ++i) {
            const KeyType key_val = key_array_[i];
            if (key_val < static_cast<KeyType>(params_.max_key)) {
                ++work_buff[key_val];
            }
        }
        
        #pragma omp barrier
        
        #pragma omp critical
        {
            for (int k = 0; k < params_.max_key; ++k) {
                key_buff1_[k] += work_buff[k];
            }
        }
        
        #pragma omp barrier
        
        #pragma omp single
        {
            for (int k = 0; k < params_.max_key - 1; ++k) {
                key_buff1_[k+1] += key_buff1_[k];
            }
        }
    }
}

template<std::integral KeyType>
void IntegerSort<KeyType>::verify_partial_results(int iteration) {
    for (int i = 0; i < params_.TEST_ARRAY_SIZE; i++) {
        const KeyType k = partial_verify_vals_[i];
        if (k > 0 && k < static_cast<KeyType>(params_.max_key)) {
            const KeyType key_rank = key_buff1_[k-1];
            bool failed = false;
            
            switch (params_.class_id) {
                case 'S':
                    if (i <= 2) {
                        if (key_rank != params_.test_rank_array[i] + iteration) {
                            failed = true;
                        } else {
                            ++passed_verification_;
                        }
                    } else {
                        if (key_rank != params_.test_rank_array[i] - iteration) {
                            failed = true;
                        } else {
                            ++passed_verification_;
                        }
                    }
                    break;
                case 'W':
                    if (i < 2) {
                        if (key_rank != params_.test_rank_array[i] + (iteration-2)) {
                            failed = true;
                        } else {
                            ++passed_verification_;
                        }
                    } else {
                        if (key_rank != params_.test_rank_array[i] - iteration) {
                            failed = true;
                        } else {
                            ++passed_verification_;
                        }
                    }
                    break;
                case 'A':
                    if (i <= 2) {
                        if (key_rank != params_.test_rank_array[i] + (iteration-1)) {
                            failed = true;
                        } else {
                            ++passed_verification_;
                        }
                    } else {
                        if (key_rank != params_.test_rank_array[i] - (iteration-1)) {
                            failed = true;
                        } else {
                            ++passed_verification_;
                        }
                    }
                    break;
                case 'B':
                    if (i == 1 || i == 2 || i == 4) {
                        if (key_rank != params_.test_rank_array[i] + iteration) {
                            failed = true;
                        } else {
                            ++passed_verification_;
                        }
                    } else {
                        if (key_rank != params_.test_rank_array[i] - iteration) {
                            failed = true;
                        } else {
                            ++passed_verification_;
                        }
                    }
                    break;
                case 'C':
                    if (i <= 2) {
                        if (key_rank != params_.test_rank_array[i] + iteration) {
                            failed = true;
                        } else {
                            ++passed_verification_;
                        }
                    } else {
                        if (key_rank != params_.test_rank_array[i] - iteration) {
                            failed = true;
                        } else {
                            ++passed_verification_;
                        }
                    }
                    break;
                case 'D':
                    if (i < 2) {
                        if (key_rank != params_.test_rank_array[i] + iteration) {
                            failed = true;
                        } else {
                            ++passed_verification_;
                        }
                    } else {
                        if (key_rank != params_.test_rank_array[i] - iteration) {
                            failed = true;
                        } else {
                            ++passed_verification_;
                        }
                    }
                    break;
            }
            
            if (failed) {
                std::cout << "Failed partial verification: iteration " << iteration 
                         << ", test key " << i << std::endl;
            }
        }
    }
}

template<std::integral KeyType>
void IntegerSort<KeyType>::full_verify() {
    if constexpr (use_buckets) {
        verify_with_buckets();
    } else {
        verify_without_buckets();
    }
    
    KeyType error_count = 0;
    
    #pragma omp parallel for reduction(+:error_count)
    for (int64_t i = 1; i < params_.total_keys; ++i) {
        if (key_array_[i-1] > key_array_[i]) {
            ++error_count;
        }
    }
    
    if (error_count != 0) {
        std::cout << "Full_verify: number of keys out of sort: " << error_count << std::endl;
    } else {
        ++passed_verification_;
    }
}

template<std::integral KeyType>
void IntegerSort<KeyType>::verify_with_buckets() {
    #pragma omp parallel for schedule(dynamic)
    for (int j = 0; j < params_.num_buckets; j++) {
        const KeyType k1 = (j > 0) ? bucket_ptrs_[j-1] : 0;
        
        for (KeyType i = k1; i < bucket_ptrs_[j]; i++) {
            const KeyType key_val = key_buff2_[i];
            if (key_val < static_cast<KeyType>(params_.max_key) && key_buff_ptr_global_[key_val] > 0) {
                const KeyType k = --key_buff_ptr_global_[key_val];
                if (k < static_cast<KeyType>(params_.total_keys)) {
                    key_array_[k] = key_val;
                }
            }
        }
    }
}

template<std::integral KeyType>
void IntegerSort<KeyType>::verify_without_buckets() {
    #pragma omp parallel for
    for (int64_t i = 0; i < params_.total_keys; ++i) {
        key_buff2_[i] = key_array_[i];
    }
    
    const int num_threads = omp_get_max_threads();
    const KeyType keys_per_thread = (params_.max_key + num_threads - 1) / num_threads;
    
    #pragma omp parallel
    {
        const int thread_id = omp_get_thread_num();
        const KeyType start_key = keys_per_thread * thread_id;
        const KeyType end_key = std::min(start_key + keys_per_thread, 
                                        static_cast<KeyType>(params_.max_key));
        
        for (int64_t i = 0; i < params_.total_keys; ++i) {
            const KeyType key_val = key_buff2_[i];
            if (key_val >= start_key && key_val < end_key && key_buff_ptr_global_[key_val] > 0) {
                const KeyType k = --key_buff_ptr_global_[key_val];
                if (k < static_cast<KeyType>(params_.total_keys)) {
                    key_array_[k] = key_val;
                }
            }
        }
    }
}

template<std::integral KeyType>
double IntegerSort<KeyType>::getExecutionTime() const noexcept {
    return execution_time_;
}

template<std::integral KeyType>
double IntegerSort<KeyType>::getMopsTotal() const noexcept {
    return static_cast<double>(params_.iterations * params_.total_keys) / 
           execution_time_ / 1'000'000.0;
}

template<std::integral KeyType>
bool IntegerSort<KeyType>::getVerificationStatus() const noexcept {
    return verified_;
}

template<std::integral KeyType>
void print_results(const IntegerSort<KeyType>& is, const ISParameters<KeyType>& params, 
                  std::string_view name, std::string_view optype) {
    const auto mops = is.getMopsTotal();
    const auto t = is.getExecutionTime();
    const auto verified = is.getVerificationStatus();
    
    // Get nanosecond timing for more precise output
    const auto time_ns = static_cast<int64_t>(t * 1e9);
    const double init_time = is.getTimer(params.T_INITIALIZATION);
    const int64_t init_time_ns = static_cast<int64_t>(init_time * 1e9);
    
    std::cout << "\n\n Verification: " << (verified ? "SUCCESSFUL" : "UNSUCCESSFUL") << "\n";
    std::cout << "\n Mop/s total = " << std::setw(12) << std::fixed << std::setprecision(2) 
              << mops << "\n";

    std::cout << "\n Benchmark completed";
    std::cout << "\n VERIFICATION " << (verified ? "SUCCESSFUL" : "UNSUCCESSFUL") << "\n\n";

    std::cout << "\n " << name << " Benchmark Completed\n";
    std::cout << " Class          =                        " << params.class_id << "\n";
    std::cout << " Size            =             " << std::setw(12) << params.total_keys << "\n";
    std::cout << " Num threads     =             " << std::setw(12) << omp_get_max_threads() << "\n";
    std::cout << " Iterations      =             " << std::setw(12) << params.iterations << "\n";
    std::cout << " Time in seconds =             " << std::setw(12) << std::fixed << std::setprecision(2) << t << "\n";
    std::cout << " Time in ns      =             " << std::setw(12) << time_ns << "\n";
    std::cout << " Mop/s total     =             " << std::setw(12) << std::fixed << std::setprecision(2) << mops << "\n";
    std::cout << " Operation type  = " << std::setw(24) << optype << "\n";
    std::cout << " Verification    =               " << (verified ? "SUCCESSFUL" : "UNSUCCESSFUL") << "\n";
    
    // Version, compiler info and dates
    std::cout << " Version         =             " << std::setw(12) << "4.1" << "\n";
    
    // Add compile date
    auto now = std::time(nullptr);
    auto tm = std::localtime(&now);
    char date_str[12];
    std::strftime(date_str, sizeof(date_str), "%d %b %Y", tm);
    std::cout << " Compile date    =             " << std::setw(12) << date_str << "\n";
    
    // Add compiler version
    #ifdef __clang__
        std::cout << " Compiler ver    =             " << std::setw(12) 
                 << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__ << "\n";
    #elif defined(__GNUC__)
        std::cout << " Compiler ver    =             " << std::setw(12)
                 << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << "\n";
    #else
        std::cout << " Compiler ver    =             " << std::setw(12) << "Unknown" << "\n";
    #endif
    
    std::cout << " C++ version     =             " << std::setw(12) << "C++23" << "\n";
    
    // Print compile options
    std::cout << "\n Compile options:\n";
    std::cout << "    CC           = g++ -std=c++23\n";
    std::cout << "    CFLAGS       = -O3 -march=native -fopenmp\n";
    
    // Add footer
    std::cout << "\n\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "    NPB-CPP (C++23 version) - " << name << " Benchmark\n";
    std::cout << "    Modern C++ implementation with enhanced parallelism\n";
    std::cout << "----------------------------------------------------------------------\n";
    
    // Section timing breakdown
    std::cout << "\n";
    std::cout << "  SECTION   Time (secs)       Time (ns)\n";
    std::cout << "  init:         " << std::fixed << std::setprecision(3) << std::setw(5) << init_time 
              << "          " << init_time_ns << "\n";
    std::cout << "  benchmark:    " << std::fixed << std::setprecision(3) << std::setw(5) << t 
              << "          " << time_ns << "  (100.00%)\n";
}

} // namespace is
} // namespace npb

// Add explicit template instantiations here
template class npb::is::IntegerSort<int64_t>;
template npb::is::ISParameters<int64_t> npb::is::load_parameters<int64_t>(char);
template void npb::is::print_results<int64_t>(const npb::is::IntegerSort<int64_t>&, const npb::is::ISParameters<int64_t>&, 
                 std::string_view, std::string_view);