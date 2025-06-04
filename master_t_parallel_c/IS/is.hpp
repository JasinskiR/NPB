#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <array>
#include <span>
#include <ranges>
#include <concepts>
#include <execution>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <bit>
#include <print>

#define USE_BUCKETS

namespace npb {
namespace is {

template<std::integral KeyType = int64_t>
struct ISParameters {
    static constexpr int T_BENCHMARKING = 0;
    static constexpr int T_INITIALIZATION = 1;
    static constexpr int T_SORTING = 2;
    static constexpr int T_TOTAL_EXECUTION = 3;
    
    int64_t total_keys;
    int max_key;
    int num_buckets;
    int iterations;
    char class_id;
    
    std::array<int64_t, 5> test_index_array;
    std::array<int64_t, 5> test_rank_array;
    static constexpr int TEST_ARRAY_SIZE = 5;
};

template<std::integral KeyType = int64_t>
class IntegerSort {
public:
    explicit IntegerSort(const ISParameters<KeyType>& params);
    ~IntegerSort() = default;
    
    void run();
    
    [[nodiscard]] double getExecutionTime() const noexcept;
    [[nodiscard]] double getMopsTotal() const noexcept;
    [[nodiscard]] bool getVerificationStatus() const noexcept;
    [[nodiscard]] double getTimer(int timer_id) const noexcept {
        if (timer_id >= 0 && timer_id < timer_values_.size()) {
            return timer_values_[timer_id];
        }
        return 0.0;
    }
    [[nodiscard]] bool getUseBuckets() const noexcept {
        return use_buckets;
    }

private:
    class RandomGenerator {
    public:
        RandomGenerator(double seed, double a);
        double next();
        
    private:
        void compute_constants();
        double x_, a_;
        double r23_, r46_, t23_, t46_;
        double a1_, a2_;
    };
    
    struct TimerGuard {
        TimerGuard(IntegerSort& is, int timer_id);
        ~TimerGuard();
        
        IntegerSort& is_;
        int timer_id_;
    };
    
    void rank(int iteration);
    void full_verify();
    void create_seq(double seed, double a);
    double find_my_seed(int kn, int np, int64_t nn, double s, double a);
    void allocate_key_buffer();
    
    void rank_with_buckets(int iteration);
    void rank_without_buckets(int iteration);
    void verify_partial_results(int iteration);
    void compute_bucket_offsets(int num_threads);
    void distribute_keys_to_buckets(int thread_id, int num_threads, int shift);
    void rank_bucket_keys(int bucket_id, KeyType num_bucket_keys);
    void accumulate_counts_globally(KeyType* work_buff);
    void verify_with_buckets();
    void verify_without_buckets();
    
    auto make_timer_guard(int timer_id);
    
    static constexpr bool use_buckets = true;
    
    ISParameters<KeyType> params_;
    bool verified_ = false;
    std::atomic<int> passed_verification_{0};
    double execution_time_ = 0.0;
    bool timers_enabled_ = false;
    
    std::vector<KeyType> key_array_;
    std::vector<KeyType> key_buff1_;
    std::vector<KeyType> key_buff2_;
    std::vector<KeyType> partial_verify_vals_;
    KeyType* key_buff_ptr_global_ = nullptr;
    
    std::vector<std::vector<KeyType>> bucket_size_;
    std::vector<KeyType> bucket_ptrs_;
    std::vector<KeyType*> key_buff1_aptr_;
    
    std::array<double, 4> timer_values_{};
};

template<std::integral KeyType = int64_t>
ISParameters<KeyType> load_parameters(char class_id = 'S');

template<std::integral KeyType = int64_t>
void print_results(const IntegerSort<KeyType>& is, const ISParameters<KeyType>& params, 
                  std::string_view name, std::string_view optype);

using ISParameters64 = ISParameters<int64_t>;
using IntegerSort64 = IntegerSort<int64_t>;

} // namespace is
} // namespace npb