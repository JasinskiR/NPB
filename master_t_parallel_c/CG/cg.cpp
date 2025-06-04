#include "cg.hpp"
#include "utils.hpp"

#include <cmath>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <ranges>
#include <execution>

namespace npb::cg {

static double tran = 314159265.0;
static double amult = 1220703125.0;

double randlc(double *x, double a) noexcept {
    static int ks = 0;
    static double r23, r46, t23, t46;
    double t1, t2, t3, t4, a1, a2, x1, x2, z;
    int i, j;

    if (ks == 0) {
        r23 = 1.0;
        r46 = 1.0;
        t23 = 1.0;
        t46 = 1.0;

        for (i = 1; i <= 23; i++) {
            r23 = 0.50 * r23;
            t23 = 2.0 * t23;
        }
        for (i = 1; i <= 46; i++) {
            r46 = 0.50 * r46;
            t46 = 2.0 * t46;
        }
        ks = 1;
    }

    t1 = r23 * a;
    j = static_cast<int>(t1);
    a1 = j;
    a2 = a - t23 * a1;

    t1 = r23 * (*x);
    j = static_cast<int>(t1);
    x1 = j;
    x2 = *x - t23 * x1;
    t1 = a1 * x2 + a2 * x1;

    j = static_cast<int>(r23 * t1);
    t2 = j;
    z = t1 - t23 * t2;
    t3 = t23 * z + a2 * x2;
    j = static_cast<int>(r46 * t3);
    t4 = j;
    *x = t3 - t46 * t4;
    return r46 * (*x);
}

constexpr int64_t convert_real_to_int(const double x, const int64_t power2) noexcept {
    return static_cast<int64_t>(power2 * x);
}

SparseMatrix::SparseMatrix(const Problem& params) 
    : params_(params)
{
    const auto na = params_.na;
    const auto nz = na * (params_.nonzer + 1) * (params_.nonzer + 1);
    
    a_.resize(nz);
    colidx_.resize(nz);
    rowstr_.resize(na + 1);
    
    x_.resize(na + 2);
    z_.resize(na + 2);
    p_.resize(na + 2);
    q_.resize(na + 2);
    r_.resize(na + 2);
    
    make_matrix();
}

double SparseMatrix::run_benchmark(npb::utils::TimerManager& timer) {
    #ifdef _OPENMP
    omp_set_num_threads(params_.num_threads);
    #endif
    
    auto initialize_vectors = [this]() {
        std::fill(x_.begin(), x_.begin() + params_.na + 1, 1.0);
        std::fill_n(q_.begin(), params_.na, 0.0);
        std::fill_n(z_.begin(), params_.na, 0.0);
        std::fill_n(r_.begin(), params_.na, 0.0);
        std::fill_n(p_.begin(), params_.na, 0.0);
    };

    auto compute_norms_and_normalize = [this]() -> std::pair<double, double> {
        double norm_temp1 = 0.0;
        double norm_temp2 = 0.0;
        
        for (int64_t j = 0; j < params_.na; j++) {
            norm_temp1 += x_[j] * z_[j];
            norm_temp2 += z_[j] * z_[j];
        }
        
        const double norm_factor = 1.0 / std::sqrt(norm_temp2);
        
        for (int64_t j = 0; j < params_.na; j++) {
            x_[j] = norm_factor * z_[j];
        }
        
        return {norm_temp1, norm_factor};
    };
    
    #pragma omp parallel
    {
        initialize_vectors();
        
        #pragma omp single
        zeta_ = 0.0;

        conjugate_gradient();
        
        #pragma omp single
        compute_norms_and_normalize();

        initialize_vectors();

        #pragma omp single
        zeta_ = 0.0;
        
        for (int it = 1; it <= params_.max_iter; it++) {
            timer.start(npb::utils::TimerManager::T_CONJ_GRAD);
            const double rnorm = conjugate_gradient();
            timer.stop(npb::utils::TimerManager::T_CONJ_GRAD);

            #pragma omp single
            {
                const auto [norm_temp1, norm_factor] = compute_norms_and_normalize();
                zeta_ = params_.shift + 1.0 / norm_temp1;
                
                if (it == 1) {
                    std::cout << "\n   iteration           ||r||                 zeta\n";
                }
                std::cout << "    " << std::setw(5) << it << "       " 
                          << std::setw(20) << std::scientific << std::setprecision(14) << rnorm
                          << std::setw(20) << std::scientific << std::setprecision(13) << zeta_ << "\n";
            }
        }
    }
    
    return timer.read(npb::utils::TimerManager::T_BENCH);
}

double SparseMatrix::conjugate_gradient() noexcept {
    constexpr int64_t cgitmax = 25;
    
    static double d, sum, rho, rho0;
    
    #pragma omp single nowait
    {
        rho = 0.0;
        sum = 0.0;
    }
    
    #pragma omp for
    for (int64_t j = 0; j < params_.na + 1; j++) {
        q_[j] = 0.0;
        z_[j] = 0.0;
        r_[j] = x_[j];
        p_[j] = r_[j];
    }
    
    #pragma omp for reduction(+:rho)
    for (int64_t j = 0; j < params_.na; j++) {
        rho += r_[j] * r_[j];
    }
    
    for (int64_t cgit = 1; cgit <= cgitmax; cgit++) {
        #pragma omp single nowait
        {
            d = 0.0;
            rho0 = rho;
            rho = 0.0;
        }
        
        #pragma omp for nowait schedule(static)
        for (int64_t j = 0; j < params_.na; j++) {
            double suml = 0.0;
            const auto row_start = rowstr_[j];
            const auto row_end = rowstr_[j+1];
            
            for (int64_t k = row_start; k < row_end; k++) {
                suml += a_[k] * p_[colidx_[k]];
            }
            q_[j] = suml;
        }
        
        #pragma omp for reduction(+:d) schedule(static)
        for (int64_t j = 0; j < params_.na; j++) {
            d += p_[j] * q_[j];
        }
        
        const double alpha = rho0 / d;
        
        #pragma omp for reduction(+:rho) schedule(static)
        for (int64_t j = 0; j < params_.na; j++) {
            z_[j] += alpha * p_[j];
            r_[j] -= alpha * q_[j];
            rho += r_[j] * r_[j];
        }
        
        const double beta = rho / rho0;
        
        #pragma omp for schedule(static)
        for (int64_t j = 0; j < params_.na; j++) {
            p_[j] = r_[j] + beta * p_[j];
        }
    }
    
    #pragma omp for nowait schedule(static)
    for (int64_t j = 0; j < params_.na; j++) {
        double suml = 0.0;
        const auto row_start = rowstr_[j];
        const auto row_end = rowstr_[j+1];
        
        for (int64_t k = row_start; k < row_end; k++) {
            suml += a_[k] * z_[colidx_[k]];
        }
        r_[j] = suml;
    }
    
    #pragma omp for reduction(+:sum) schedule(static)
    for (int64_t j = 0; j < params_.na; j++) {
        const double suml = x_[j] - r_[j];
        sum += suml * suml;
    }
    
    #pragma omp single
    sum = std::sqrt(sum);
    
    return sum;
}

void SparseMatrix::make_matrix() {
    const auto nz = params_.na * (params_.nonzer + 1) * (params_.nonzer + 1);
    constexpr int64_t firstrow = 0;
    const int64_t lastrow = params_.na - 1;
    constexpr int64_t firstcol = 0;
    const int64_t lastcol = params_.na - 1;
    
    std::vector<int64_t> arow(params_.na);
    std::vector<std::vector<int64_t>> acol(params_.na, std::vector<int64_t>(params_.nonzer + 1));
    std::vector<std::vector<double>> aelt(params_.na, std::vector<double>(params_.nonzer + 1));
    std::vector<int64_t> iv(params_.na);
    
    [[maybe_unused]] const double zeta_local = randlc(&tran, amult);
    
    int64_t nn1 = 1;
    while (nn1 < params_.na) {
        nn1 *= 2;
    }
    
    for (int64_t iouter = 0; iouter < params_.na; iouter++) {
        int64_t nzv = params_.nonzer;
        
        std::vector<double> vc(params_.nonzer + 1);
        std::vector<int64_t> ivc(params_.nonzer + 1);
        
        generate_sparse_vector(params_.na, nzv, nn1, vc, ivc);
        vector_set(params_.na, vc, ivc, &nzv, iouter + 1, 0.5);
        
        arow[iouter] = nzv;
        for (int64_t ivelt = 0; ivelt < nzv; ivelt++) {
            acol[iouter][ivelt] = ivc[ivelt] - 1;
            aelt[iouter][ivelt] = vc[ivelt];
        }
    }
    
    std::vector<int64_t> nzloc(params_.na);
    sparse_matrix_assembly(
        a_, colidx_, rowstr_,
        params_.na, nz, params_.nonzer,
        arow, acol, aelt,
        firstrow, lastrow,
        nzloc,
        params_.rcond, params_.shift
    );
    
    const auto adjustment_range = lastrow - firstrow + 1;
    for (int64_t j = 0; j < adjustment_range; j++) {
        const auto row_start = rowstr_[j];
        const auto row_end = rowstr_[j+1];
        for (int64_t k = row_start; k < row_end; k++) {
            colidx_[k] -= firstcol;
        }
    }
}

void SparseMatrix::sparse_matrix_assembly(
    std::vector<double>& a, 
    std::vector<int64_t>& colidx, 
    std::vector<int64_t>& rowstr,
    const int64_t n, const int64_t nz, [[maybe_unused]] const int64_t nozer,
    const std::vector<int64_t>& arow,
    const std::vector<std::vector<int64_t>>& acol,
    const std::vector<std::vector<double>>& aelt,
    const int64_t firstrow, const int64_t lastrow,
    std::vector<int64_t>& nzloc,
    const double rcond, const double shift
) {
    const int64_t nrows = lastrow - firstrow + 1;
    
    std::fill_n(rowstr.begin(), nrows + 1, 0);
    
    for (int64_t i = 0; i < n; i++) {
        for (int64_t nza = 0; nza < arow[i]; nza++) {
            const int64_t j = acol[i][nza] + 1;
            rowstr[j] += arow[i];
        }
    }
    
    rowstr[0] = 0;
    for (int64_t j = 1; j < nrows + 1; j++) {
        rowstr[j] += rowstr[j-1];
    }
    const int64_t nza_final = rowstr[nrows] - 1;
    
    if (nza_final > nz) {
        throw std::runtime_error("Space for matrix elements exceeded in sparse assembly");
    }
    
    for (int64_t j = 0; j < nrows; j++) {
        const auto row_start = rowstr[j];
        const auto row_end = rowstr[j+1];
        std::fill(a.begin() + row_start, a.begin() + row_end, 0.0);
        std::fill(colidx.begin() + row_start, colidx.begin() + row_end, -1);
        nzloc[j] = 0;
    }
    
    double size = 1.0;
    const double ratio = std::pow(rcond, 1.0 / static_cast<double>(n));
    
    for (int64_t i = 0; i < n; i++) {
        for (int64_t nza = 0; nza < arow[i]; nza++) {
            const int64_t j = acol[i][nza];
            const double scale = size * aelt[i][nza];
            
            for (int64_t nzrow = 0; nzrow < arow[i]; nzrow++) {
                const int64_t jcol = acol[i][nzrow];
                double va = aelt[i][nzrow] * scale;
                
                if (jcol == j && j == i) {
                    va += rcond - shift;
                }
                
                bool inserted = false;
                int64_t k;
                
                for (k = rowstr[j]; k < rowstr[j+1]; k++) {
                    if (colidx[k] > jcol) {
                        for (int64_t kk = rowstr[j+1]-2; kk >= k; kk--) {
                            if (colidx[kk] > -1) {
                                a[kk+1] = a[kk];
                                colidx[kk+1] = colidx[kk];
                            }
                        }
                        colidx[k] = jcol;
                        a[k] = 0.0;
                        inserted = true;
                        break;
                    } else if (colidx[k] == -1) {
                        colidx[k] = jcol;
                        inserted = true;
                        break;
                    } else if (colidx[k] == jcol) {
                        nzloc[j]++;
                        inserted = true;
                        break;
                    }
                }
                
                if (!inserted) {
                    throw std::runtime_error("Internal error in sparse assembly at i=" + std::to_string(i));
                }
                
                a[k] += va;
            }
        }
        size *= ratio;
    }
    
    for (int64_t j = 1; j < nrows; j++) {
        nzloc[j] += nzloc[j-1];
    }
    
    for (int64_t j = 0; j < nrows; j++) {
        const int64_t j1 = (j > 0) ? rowstr[j] - nzloc[j-1] : 0;
        const int64_t j2 = rowstr[j+1] - nzloc[j];
        int64_t nza = rowstr[j];
        
        for (int64_t k = j1; k < j2; k++) {
            a[k] = a[nza];
            colidx[k] = colidx[nza];
            nza++;
        }
    }
    
    for (int64_t j = 1; j < nrows + 1; j++) {
        rowstr[j] -= nzloc[j-1];
    }
}

void SparseMatrix::generate_sparse_vector(
    const int64_t n, const int64_t nz, const int64_t nn1,
    std::vector<double>& v, 
    std::vector<int64_t>& iv
) noexcept {
    int64_t nzv = 0;
    
    while (nzv < nz) {
        const double vecelt = randlc(&tran, amult);
        const double vecloc = randlc(&tran, amult);
        const int64_t i = convert_real_to_int(vecloc, nn1) + 1;
        
        if (i > n) continue;
        
        const bool already_exists = std::any_of(
            iv.begin(), iv.begin() + nzv,
            [i](const int64_t existing) { return existing == i; }
        );
        
        if (already_exists) continue;
        
        v[nzv] = vecelt;
        iv[nzv] = i;
        nzv++;
    }
}

void SparseMatrix::vector_set(
    [[maybe_unused]] const int64_t n, 
    std::vector<double>& v, 
    std::vector<int64_t>& iv, 
    int64_t* nzv, 
    const int64_t i, 
    const double val
) noexcept {
    const auto it = std::find(iv.begin(), iv.begin() + *nzv, i);
    
    if (it != iv.begin() + *nzv) {
        const auto index = std::distance(iv.begin(), it);
        v[index] = val;
    } else {
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

double SparseMatrix::get_mflops(const double execution_time) const noexcept {
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
    
    const double zeta_verify_value = get_zeta_verify_value();
    const double err = std::abs((zeta_ - zeta_verify_value) / zeta_verify_value);
    
    return (err <= epsilon);
}

} // namespace npb::cg