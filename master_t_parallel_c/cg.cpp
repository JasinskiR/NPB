#include "cg.hpp"
#include "utils.hpp"

#include <cmath>
#include <iostream>
#include <algorithm>
#include <execution>
#include <stdexcept>
#include <random>

namespace npb::cg {

// Convert a double in [0,1] to an integer power of 2
int64_t convert_real_to_int(double x, int64_t power2) {
    return static_cast<int64_t>(power2 * x);
}

SparseMatrix::SparseMatrix(const Problem& params) 
    : params_(params)
{
    // Set sizes based on problem parameters
    auto na = params_.na;
    auto nz = na * (params_.nonzer + 1) * (params_.nonzer + 1);
    
    // Initialize matrix and vector data structures
    a_.resize(nz);
    colidx_.resize(nz);
    rowstr_.resize(na + 1);
    
    x_.resize(na + 2, 1.0);  // Initialize to 1.0
    z_.resize(na + 2, 0.0);
    p_.resize(na + 2, 0.0);
    q_.resize(na + 2, 0.0);
    r_.resize(na + 2, 0.0);
    
    // Create the sparse matrix
    make_matrix();
}

double SparseMatrix::run_benchmark() {
    // Initialize vectors
    std::fill(x_.begin(), x_.end(), 1.0);
    std::fill(z_.begin(), z_.end(), 0.0);
    std::fill(p_.begin(), p_.end(), 0.0);
    std::fill(q_.begin(), q_.end(), 0.0);
    std::fill(r_.begin(), r_.end(), 0.0);
    
    // Do one iteration untimed to initialize data
    conjugate_gradient();
    
    // Reset for the actual benchmark
    std::fill(x_.begin(), x_.end(), 1.0);
    std::fill(z_.begin(), z_.end(), 0.0);
    std::fill(p_.begin(), p_.end(), 0.0);
    std::fill(q_.begin(), q_.end(), 0.0);
    std::fill(r_.begin(), r_.end(), 0.0);
    zeta_ = 0.0;  // Reset class member instead of creating a new local variable
    
    // Run main benchmark
    npb::utils::TimerManager timer;
    timer.enable();
    timer.start(npb::utils::TimerManager::T_BENCH);
    
    for (int it = 1; it <= params_.max_iter; it++) {
        timer.start(npb::utils::TimerManager::T_CONJ_GRAD);
        double rnorm = conjugate_gradient();
        timer.stop(npb::utils::TimerManager::T_CONJ_GRAD);
        
        // Calculate zeta
        double norm_temp1 = 0.0;
        double norm_temp2 = 0.0;
        
        for (int64_t j = 0; j < params_.na; j++) {
            norm_temp1 += x_[j] * z_[j];
            norm_temp2 += z_[j] * z_[j];
        }
        
        norm_temp2 = 1.0 / std::sqrt(norm_temp2);
        zeta_ = params_.shift + 1.0 / norm_temp1;  // Update class member
        
        // Print iteration info
        if (it == 1) {
            std::cout << "\n   iteration           ||r||                 zeta\n";
        }
        std::cout << "    " << std::setw(5) << it << "       " 
                  << std::setw(20) << std::scientific << std::setprecision(14) << rnorm
                  << std::setw(20) << std::scientific << std::setprecision(13) << zeta_ << "\n";
        
        // Normalize z to obtain x
        for (int64_t j = 0; j < params_.na; j++) {
            x_[j] = norm_temp2 * z_[j];
        }
    }
    
    timer.stop(npb::utils::TimerManager::T_BENCH);
    return timer.read(npb::utils::TimerManager::T_BENCH);
}

double SparseMatrix::conjugate_gradient() {
    constexpr int64_t cgitmax = 25; // Max CG iterations
    
    // Initialize the CG algorithm
    std::fill(q_.begin(), q_.end(), 0.0);
    std::fill(z_.begin(), z_.end(), 0.0);
    
    // Copy x to r for residual calculation
    std::copy(x_.begin(), x_.end(), r_.begin());
    std::copy(r_.begin(), r_.end(), p_.begin());
    
    // Calculate initial rho = r.r
    double rho = 0.0;
    // Use sequential calculation for initial rho to match reference implementation exactly
    for (int64_t j = 0; j < params_.na; j++) {
        rho += r_[j] * r_[j];
    }
    
    // Main CG iteration loop
    for (int64_t cgit = 1; cgit <= cgitmax; cgit++) {
        // q = A.p (sparse matrix-vector multiply)
        #pragma omp parallel for schedule(static)
        for (int64_t j = 0; j < params_.na; j++) {
            double sum = 0.0;
            for (int64_t k = rowstr_[j]; k < rowstr_[j+1]; k++) {
                sum += a_[k] * p_[colidx_[k]];
            }
            q_[j] = sum;
        }
        
        // Calculate d = p.q
        double d = 0.0;
        #pragma omp parallel for reduction(+:d) schedule(static)
        for (int64_t j = 0; j < params_.na; j++) {
            d += p_[j] * q_[j];
        }
        
        // Calculate alpha = rho / d
        double alpha = rho / d;
        
        // Save current rho
        double rho0 = rho;
        
        // Update z and r, recalculate rho
        rho = 0.0;
        #pragma omp parallel for reduction(+:rho) schedule(static)
        for (int64_t j = 0; j < params_.na; j++) {
            z_[j] += alpha * p_[j];
            r_[j] -= alpha * q_[j];
            rho += r_[j] * r_[j];
        }
        
        // Calculate beta = rho / rho0
        double beta = rho / rho0;
        
        // Update p = r + beta*p
        #pragma omp parallel for schedule(static)
        for (int64_t j = 0; j < params_.na; j++) {
            p_[j] = r_[j] + beta * p_[j];
        }
    }
    
    // Compute residual norm explicitly: ||r|| = ||x - A.z||
    // First, form A.z
    #pragma omp parallel for
    for (int64_t j = 0; j < params_.na; j++) {
        double sum = 0.0;
        for (int64_t k = rowstr_[j]; k < rowstr_[j+1]; k++) {
            sum += a_[k] * z_[colidx_[k]];
        }
        r_[j] = sum;
    }
    
    // Calculate ||x - A.z||
    double sum = 0.0;
    #pragma omp parallel for reduction(+:sum)
    for (int64_t j = 0; j < params_.na; j++) {
        double d = x_[j] - r_[j];
        sum += d * d;
    }
    
    return std::sqrt(sum);
}

void SparseMatrix::make_matrix() {
    // Initialize global variables for this method
    int64_t nz = params_.na * (params_.nonzer + 1) * (params_.nonzer + 1);
    int64_t firstrow = 0;
    int64_t lastrow = params_.na - 1;
    int64_t firstcol = 0;
    int64_t lastcol = params_.na - 1;
    
    // Generate random matrix with prescribed sparsity pattern
    std::vector<int64_t> arow(params_.na);
    std::vector<std::vector<int64_t>> acol(params_.na, std::vector<int64_t>(params_.nonzer + 1));
    std::vector<std::vector<double>> aelt(params_.na, std::vector<double>(params_.nonzer + 1));
    std::vector<int64_t> iv(params_.na);
    
    // Random number generator state
    double tran = 314159265.0;
    double amult = 1220703125.0;
    
    // Find smallest power of 2 not less than n
    int64_t nn1 = 1;
    while (nn1 < params_.na) {
        nn1 *= 2;
    }
    
    // Generate nonzero positions for the sparse matrix
    for (int64_t iouter = 0; iouter < params_.na; iouter++) {
        int64_t nzv = params_.nonzer;
        
        // Generate a sparse vector with nzv nonzeros
        std::vector<double> vc(params_.nonzer + 1);
        std::vector<int64_t> ivc(params_.nonzer + 1);
        
        generate_sparse_vector(params_.na, nzv, nn1, vc, ivc);
        
        // Set iouter+1 element to 0.5
        vector_set(params_.na, vc, ivc, &nzv, iouter + 1, 0.5);
        
        arow[iouter] = nzv;
        for (int64_t ivelt = 0; ivelt < nzv; ivelt++) {
            acol[iouter][ivelt] = ivc[ivelt] - 1;
            aelt[iouter][ivelt] = vc[ivelt];
        }
    }
    
    // Assemble the sparse matrix from the elements
    std::vector<int64_t> nzloc(params_.na);
    sparse_matrix_assembly(
        a_, colidx_, rowstr_,
        params_.na, nz, params_.nonzer,
        arow, acol, aelt,
        firstrow, lastrow,
        nzloc,
        params_.rcond, params_.shift
    );
    
    // Adjust column indices for local indexing
    for (int64_t j = 0; j < lastrow - firstrow + 1; j++) {
        for (int64_t k = rowstr_[j]; k < rowstr_[j+1]; k++) {
            colidx_[k] = colidx_[k] - firstcol;
        }
    }
}

void SparseMatrix::sparse_matrix_assembly(
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
) {
    int64_t nrows = lastrow - firstrow + 1;
    
    // Initialize rowstr and nzloc
    std::fill(rowstr.begin(), rowstr.begin() + nrows + 1, 0);
    std::fill(nzloc.begin(), nzloc.begin() + nrows, 0);
    
    // Count the number of triples in each row
    for (int64_t i = 0; i < n; i++) {
        for (int64_t nza = 0; nza < arow[i]; nza++) {
            int64_t j = acol[i][nza] + 1;
            rowstr[j] += arow[i];
        }
    }
    
    // Cumulative sum to get rowstr values
    rowstr[0] = 0;
    for (int64_t j = 1; j < nrows + 1; j++) {
        rowstr[j] += rowstr[j-1];
    }
    
    int64_t nza = rowstr[nrows] - 1;
    if (nza > nz) {
        throw std::runtime_error("Space for matrix elements exceeded in sparse");
    }
    
    // Initialize a and colidx (using nz instead of nrows)
    std::fill(a.begin(), a.begin() + nza + 1, 0.0);
    std::fill(colidx.begin(), colidx.begin() + nza + 1, -1);
    
    // Generate the sparse matrix by summing elements with same indices
    double size = 1.0;
    double ratio = std::pow(rcond, (1.0 / static_cast<double>(n)));
    
    for (int64_t i = 0; i < n; i++) {
        for (int64_t nza = 0; nza < arow[i]; nza++) {
            int64_t j = acol[i][nza];
            
            // Process each element of the sparse matrix
            double scale = size * aelt[i][nza];
            for (int64_t nzrow = 0; nzrow < arow[i]; nzrow++) {
                int64_t jcol = acol[i][nzrow];
                double val = aelt[i][nzrow] * scale;
                
                // Add identity * rcond to bound eigenvalues from below
                if (jcol == j && j == i) {
                    val += rcond - shift;
                }
                
                // Find position for this element in the matrix
                bool inserted = false;
                int64_t k = 0; // Declare k outside the loop so it stays in scope

                for (k = rowstr[j]; k < rowstr[j+1]; k++) {
                    if (colidx[k] > jcol) {
                        // Insert in order - make sure we don't go out of bounds
                        for (int64_t kk = rowstr[j+1] - 2; kk >= k && kk >= 0; kk--) {
                            if (colidx[kk] > -1 && kk+1 < a.size()) {
                                a[kk+1] = a[kk];
                                colidx[kk+1] = colidx[kk];
                            }
                        }
                        colidx[k] = jcol;
                        a[k] = 0.0;
                        inserted = true;
                        break;
                    } else if (colidx[k] == -1) {
                        // Fill empty spot
                        colidx[k] = jcol;
                        inserted = true;
                        break;
                    } else if (colidx[k] == jcol) {
                        // Already exists, mark as duplicate
                        nzloc[j]++;
                        inserted = true;
                        break;
                    }
                }

                if (!inserted) {
                    throw std::runtime_error("Internal error in sparse matrix assembly");
                }

                // Add the value to the element
                if (colidx[k] == jcol) {
                    a[k] += val;
                }
            }
        }
        
        // Scale for next iteration
        size *= ratio;
    }
    
    // Remove duplicates and create final sparse matrix
    for (int64_t j = 1; j < nrows; j++) {
        nzloc[j] += nzloc[j-1];
    }
    
    for (int64_t j = 0; j < nrows; j++) {
        int64_t j1 = (j > 0) ? rowstr[j] - nzloc[j-1] : 0;
        int64_t j2 = rowstr[j+1] - nzloc[j];
        int64_t nza = rowstr[j];
        
        for (int64_t k = j1; k < j2; k++) {
            a[k] = a[nza];
            colidx[k] = colidx[nza];
            nza++;
        }
    }
    
    // Adjust rowstr to account for removed duplicates
    for (int64_t j = 1; j < nrows + 1; j++) {
        rowstr[j] = rowstr[j] - nzloc[j-1];
    }
}

void SparseMatrix::generate_sparse_vector(
    int64_t n, int64_t nz, int64_t nn1,
    std::vector<double>& v, 
    std::vector<int64_t>& iv
) {
    // Random number generator state
    static double tran = 314159265.0;
    static double amult = 1220703125.0;
    
    int64_t nzv = 0;
    
    while (nzv < nz) {
        // Generate random value
        double vecelt = npb::utils::RandomGenerator::randlc(&tran, amult);
        
        // Generate random position (an integer between 1 and n)
        double vecloc = npb::utils::RandomGenerator::randlc(&tran, amult);
        int64_t i = convert_real_to_int(vecloc, nn1) + 1;
        
        if (i > n) continue;
        
        // Check if this position was already generated
        bool already_generated = false;
        for (int64_t ii = 0; ii < nzv; ii++) {
            if (iv[ii] == i) {
                already_generated = true;
                break;
            }
        }
        
        if (already_generated) continue;
        
        // Add to the vector
        v[nzv] = vecelt;
        iv[nzv] = i;
        nzv++;
    }
}

void SparseMatrix::vector_set(
    int64_t n, 
    std::vector<double>& v, 
    std::vector<int64_t>& iv, 
    int64_t* nzv, 
    int64_t i, 
    double val
) {
    // Check if position i is already in the vector
    bool set = false;
    for (int64_t k = 0; k < *nzv; k++) {
        if (iv[k] == i) {
            v[k] = val;
            set = true;
            break;
        }
    }
    
    // If not found, add it
    if (!set) {
        v[*nzv] = val;
        iv[*nzv] = i;
        (*nzv)++;
    }
}

double SparseMatrix::get_zeta_verify_value() const noexcept {
    switch (params_.problem_class) {
        case 'S': return 8.5971775078648;
        case 'W': return 10.362595087124;
        case 'A': return 17.130235054029;
        case 'B': return 22.712745482631;
        case 'C': return 28.973605592845;
        case 'D': return 52.514532105794;
        case 'E': return 77.522164599383;
        default:  return 0.0;
    }
}

double SparseMatrix::get_mflops(double execution_time) const noexcept {
    if (execution_time == 0.0) {
        return 0.0;
    }
    
    return (2.0 * params_.max_iter * params_.na) *
           (3.0 + (params_.nonzer * (params_.nonzer + 1)) +
            25.0 * (5.0 + (params_.nonzer * (params_.nonzer + 1))) + 3.0) /
           execution_time / 1000000.0;
}

bool SparseMatrix::verify() const noexcept {
    constexpr double epsilon = 1.0e-10;
    
    if (params_.problem_class == 'U') {
        return false;
    }
    
    double zeta_verify_value = get_zeta_verify_value();
    double err = std::abs((zeta_ - zeta_verify_value) / zeta_verify_value);
    
    return (err <= epsilon);
}

} // namespace npb::cg