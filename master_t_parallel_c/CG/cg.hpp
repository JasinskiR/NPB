#pragma once

#include "utils.hpp" 

#include <vector>
#include <span>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <execution>
#include <thread>
#include <future>
#include <atomic>
#include <barrier>
#include <ranges>
#include <algorithm>
#include <numeric>
#include <functional>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace npb::cg {

struct Problem {
    int64_t na;          // size of matrix A
    int64_t nonzer;      // number of nonzeros per row
    double shift;        // shift for diagonal
    double rcond;        // conditioning parameter
    int64_t max_iter;    // maximum iterations
    char problem_class;  // problem class S, W, A, B, C, D, E, or U
    int num_threads;     // number of threads to use
};

class SparseMatrix {
public:
    // Constructors
    explicit SparseMatrix(const Problem& params);
    
    // Run the benchmark
    double run_benchmark(npb::utils::TimerManager& timer);
    
    // Get verification value
    [[nodiscard]] double get_zeta() const noexcept { return zeta_; }
    
    // Get verification value based on problem class
    [[nodiscard]] double get_zeta_verify_value() const noexcept;
    
    // Get problem details
    [[nodiscard]] const Problem& get_problem() const noexcept { return params_; }
    
    // Get MFLOPS
    [[nodiscard]] double get_mflops(double execution_time) const noexcept;
    
    // Verification
    [[nodiscard]] bool verify() const noexcept;

private:
    // Problem parameters
    Problem params_;
    
    // Matrix data
    std::vector<double> a_;           // Matrix elements
    std::vector<int64_t> colidx_;     // Column indices
    std::vector<int64_t> rowstr_;     // Row pointers
    
    // Vectors
    std::vector<double> x_;           // Solution vector
    std::vector<double> z_;           // Temporary vector
    std::vector<double> p_;           // Conjugate direction
    std::vector<double> q_;           // Temporary vector
    std::vector<double> r_;           // Residual
    
    // Scalars for benchmark
    double zeta_{0.0};
    
    // Matrix generation helpers
    void make_matrix();
    void sparse_matrix_assembly(
        std::vector<double>& a, 
        std::vector<int64_t>& colidx, 
        std::vector<int64_t>& rowstr,
        int64_t n, int64_t nz, int64_t nozer,
        const std::vector<int64_t>& arow,
        const std::vector<std::vector<int64_t>>& acol,
        const std::vector<std::vector<double>>& aelt,
        int64_t firstrow, int64_t lastrow,
        std::vector<int64_t>& nzloc,
        double rcond, double shift
    );
    
    void generate_sparse_vector(
        int64_t n, int64_t nz, int64_t nn1,
        std::vector<double>& v, 
        std::vector<int64_t>& iv
    ) noexcept;
    
    void vector_set(
        int64_t n, 
        std::vector<double>& v, 
        std::vector<int64_t>& iv, 
        int64_t* nzv, 
        int64_t i, 
        double val
    ) noexcept;
    
    // Core algorithm
    double conjugate_gradient() noexcept;
};

// Helper functions
constexpr int64_t convert_real_to_int(double x, int64_t power2) noexcept;

} // namespace npb::cg