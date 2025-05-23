use std::env;
use std::time::Instant;
use chrono::Local;
use rayon::prelude::*;

use common::print_results;
use common::randdp;

const CLASS: &str = "b";
const NA: i32 = 75000;
const NONZER: i32 = 13;
const NITER: i32 = 75;
const SHIFT: f64 = 60.0;
const NZ: i32 = NA * (NONZER + 1) * (NONZER + 1);
const NAZ: i32 = NA * (NONZER + 1);

const NPBVERSION: &str = "4.1.2";
const COMPILETIME: &str = "2025-05-16T16:58:19.649847+02:00";
const COMPILERVERSION: &str = "rustc 1.70.0-nightly";
const LIBVERSION: &str = "1";
const CS1: &str = "";
const CS2: &str = "";
const CS3: &str = "";
const CS4: &str = "";
const CS5: &str = "";
const CS6: &str = "";
const CS7: &str = "";

const RCOND: f64 = 0.1;

fn main() {
    let init_timer = Instant::now();
    
    let args: Vec<String> = env::args().collect();
    let num_threads = if args.len() > 1 {
        args[1].parse::<usize>().unwrap_or(1)
    } else {
        1
    };

    // Configure Rayon to use the specified number of threads
    rayon::ThreadPoolBuilder::new()
        .num_threads(num_threads)
        .build_global()
        .unwrap();

    println!(" Using {} threads", num_threads);
    
    let mut colidx: Vec<i32> = vec![0; NZ as usize];
    let mut rowstr: Vec<i32> = vec![0; (NA + 1) as usize];
    let mut iv: Vec<i32> = vec![0; NA as usize];
    let mut arow: Vec<i32> = vec![0; NA as usize];
    let mut acol: Vec<i32> = vec![0; NAZ as usize];
    let mut aelt: Vec<f64> = vec![0.0; NAZ as usize];
    let mut a: Vec<f64> = vec![0.0; NZ as usize];
    let mut x: Vec<f64> = vec![1.0; (NA + 2) as usize];
    let mut z: Vec<f64> = vec![0.0; (NA + 2) as usize];
    let mut p: Vec<f64> = vec![0.0; (NA + 2) as usize];
    let mut q: Vec<f64> = vec![0.0; (NA + 2) as usize];
    let mut r: Vec<f64> = vec![0.0; (NA + 2) as usize];

    let mut naa: i32 = 0;
    let mut nzz: i32 = 0;
    let firstrow: i32 = 0;
    let lastrow: i32 = NA - 1;
    let firstcol: i32 = 0;
    let lastcol: i32 = NA - 1;
    let mut amult: f64 = 0.0;
    let mut tran: f64 = 0.0;
    
    let (mut zeta, mut norm_temp1, mut norm_temp2, mut t, mflops, zeta_verify_value, epsilon, err): (f64, f64, f64, f64, f64, f64, f64, f64);
    let mut rnorm: f64 = 0.0;
    let verified: bool;

    zeta_verify_value = match CLASS {
        "s"=>8.5971775078648,
        "w"=>10.362595087124,
        "a"=>17.130235054029,
        "b"=>22.712745482631,
        "c"=>28.973605592845,
        "d"=>52.514532105794,
        "e"=>77.522164599383,
        _=>8.5971775078648
    };

    println!("\n\n NAS Parallel Benchmarks 4.1 Parallel Rust version - CG Benchmark\n");
    println!(" Size: {}", &NA);
    println!(" Iterations: {}", &NITER);

    naa = NA;
    nzz = NZ;

    tran  = 314159265.0;
    amult = 1220703125.0;
    zeta  = randdp::randlc(&mut tran, amult);
    
    makea(&mut naa, &mut nzz, &mut a, &mut colidx, &mut rowstr, &firstrow, &lastrow, &firstcol, &lastcol, &mut arow, &mut acol, &mut aelt, &mut iv, &mut tran, &amult);

    // Adjust colidx to match sequential version
    for j in 0..=(lastcol - firstrow) {
        for k in rowstr[j as usize]..rowstr[(j + 1) as usize] {
            if colidx[k as usize] >= 0 {
                colidx[k as usize] = colidx[k as usize] - firstcol;
            }
        }
    }

    // Initialize vectors
    for i in 0..=NA {
        x[i as usize] = 1.0;
    }
    
    for j in 0..=(lastcol - firstcol) {
        q[j as usize] = 0.0;
        z[j as usize] = 0.0;
        r[j as usize] = 0.0;
        p[j as usize] = 0.0;
    }
    
    zeta = 0.0;

    conj_grad(&mut colidx, &mut rowstr, &mut x, &mut z, &mut a, &mut p, &mut q, &mut r, &mut rnorm, &naa, &lastcol, &firstcol, &lastrow, &firstrow);

    // Compute the length of our active portion:
    let len = (lastcol - firstcol + 1) as usize;

    // Parallel dot‐product for norm_temp1:
    norm_temp1 = x[..len]
        .par_iter()
        .zip(&z[..len])
        .map(|(&xi, &zi)| xi * zi)
        .sum();

    // Parallel sum of squares for norm_temp2:
    norm_temp2 = z[..len]
        .par_iter()
        .map(|&zi| zi * zi)
        .sum();

    // Avoid division by zero
    if norm_temp2.abs() < 1e-30 {
        norm_temp2 = 1e-30;
    }

    norm_temp2 = 1.0 / norm_temp2.sqrt();

    // Parallel update of x
    x[..len].par_iter_mut()
        .zip(&z[..len])
        .for_each(|(xi, &zi)| {
            *xi = norm_temp2 * zi;
        });

    // Reset x to match sequential version
    for i in 0..=NA {
        x[i as usize] = 1.0;
    }
    
    zeta = 0.0;

    t = init_timer.elapsed().as_secs_f64();
    println!(" Initialization time = {} seconds", &t);
    let bench_timer = Instant::now();

    // Main iteration - minimal parallelization
    for it in 1..=NITER {
        conj_grad(&mut colidx, &mut rowstr, &mut x, &mut z, &mut a, &mut p, &mut q, &mut r, &mut rnorm, &naa, &lastcol, &firstcol, &lastrow, &firstrow);
        
        // Parallel dot‐product for norm_temp1:
        norm_temp1 = x[..len]
            .par_iter()
            .zip(&z[..len])
            .map(|(&xi, &zi)| xi * zi)
            .sum();

        // Parallel sum of squares for norm_temp2:
        norm_temp2 = z[..len]
            .par_iter()
            .map(|&zi| zi * zi)
            .sum();

        // Avoid division by zero
        if norm_temp2.abs() < 1e-30 {
            norm_temp2 = 1e-30;
        }

        norm_temp2 = 1.0 / norm_temp2.sqrt();

        // Avoid division by zero
        if norm_temp1.abs() < 1e-30 {
            norm_temp1 = 1e-30;
        }
        
        zeta = SHIFT + 1.0 / norm_temp1;

        if it == 1 {
            println!("\n   iteration           ||r||                 zeta");
        }
        println!("    {}       {}   {}", &it, &rnorm, &zeta);
        
        // Parallel update of x instead of sequential
        x[..=(lastcol - firstcol) as usize].par_iter_mut()
            .zip(&z[..=(lastcol - firstcol) as usize])
            .for_each(|(xi, &zi)| {
                *xi = norm_temp2 * zi;
            });
    }

    t = bench_timer.elapsed().as_secs_f64();
    println!(" Benchmark completed");

    epsilon = 0.0000000001;
    err = (zeta - zeta_verify_value).abs() / zeta_verify_value;

    if err <= epsilon {
        verified = true;
        println!(" VERIFICATION SUCCESSFUL");
        println!(" Zeta is    {}", zeta);
        println!(" Error is   {}", err);
    } else {
        verified = false;
        println!(" VERIFICATION FAILED");
        println!(" Zeta is    {}", zeta);
        println!(" Error is   {}", err);
    }

    if t != 0.0 {
        mflops = (2.0 * NITER as f64 * NA as f64) * 
                 (3.0 + (NONZER as f64 * (NONZER as f64 + 1.0)) + 
                  25.0 * (5.0 + (NONZER as f64 * (NONZER as f64 + 1.0))) + 3.0) / t / 1000000.0;
    } else {
        mflops = 0.0;
    }
    
    // Update the print_results call to show the actual thread count
    print_results::rust_print_results("CG", CLASS, NA.try_into().unwrap(), 0, 0, NITER, t, mflops, 
                                    "          floating point", verified, NPBVERSION, 
                                    COMPILETIME, COMPILERVERSION, LIBVERSION, num_threads.to_string().as_str(), 
                                    CS1, CS2, CS3, CS4, CS5, CS6, CS7);
}
									

fn conj_grad(colidx: &mut Vec<i32>, rowstr: &mut Vec<i32>, x: &mut Vec<f64>, z: &mut Vec<f64>, 
    a: &mut Vec<f64>, p: &mut Vec<f64>, q: &mut Vec<f64>, r: &mut Vec<f64>, 
    rnorm: &mut f64, naa: &i32, lastcol: &i32, firstcol: &i32, 
    lastrow: &i32, firstrow: &i32) {
    let cgitmax: i32 = 25;
    let (mut d, mut sum, mut rho, mut rho0, mut alpha, mut beta): (f64, f64, f64, f64, f64, f64);

    // Initialize vectors - use sequential initialization for now
    for j in 0..=*naa {
        let j = j as usize;
        q[j] = 0.0;
        z[j] = 0.0;
        r[j] = x[j];
        p[j] = r[j];
    }

    // Initial rho calculation in parallel
    rho = (0..=(*lastcol - *firstcol))
        .into_par_iter()
        .map(|j| {
            let idx = j as usize;
            r[idx] * r[idx]
        })
        .sum();

    for _cgit in 1..=cgitmax {
        // Use chunks to parallelize computing q
        // This creates non-overlapping mutable slices
        q.par_chunks_mut(1)
            .enumerate()
            .for_each(|(j, q_slice)| {
                if j <= (*lastrow - *firstrow) as usize {
                    let mut sum = 0.0;
                    for k in rowstr[j]..rowstr[j + 1] {
                        let k = k as usize;
                        let cidx = colidx[k];
                        if cidx >= 0 && (cidx as usize) < p.len() {
                            sum += a[k] * p[cidx as usize];
                        }
                    }
                    q_slice[0] = sum;
                }
            });

        // Calculate d in parallel
        d = (0..=(*lastcol - *firstcol))
            .into_par_iter()
            .map(|j| {
                let j = j as usize;
                p[j] * q[j]
            })
            .sum();

        // Avoid division by zero
        if d.abs() < 1e-30 {
            d = 1e-30;
        }

        alpha = rho / d;
        rho0 = rho;

        // Update vectors z and r using mutable iterators
        let range = 0..=(*lastcol - *firstcol) as usize;
        let z_slice = &mut z[range.clone()];
        let r_slice = &mut r[range.clone()];
        let p_slice = &p[range.clone()];
        let q_slice = &q[range.clone()];

        z_slice.par_iter_mut()
            .zip(p_slice)
            .for_each(|(z_val, &p_val)| {
                *z_val = *z_val + alpha * p_val;
            });

        r_slice.par_iter_mut()
            .zip(q_slice)
            .for_each(|(r_val, &q_val)| {
                *r_val = *r_val - alpha * q_val;
            });

        // Calculate new rho in parallel
        rho = r_slice.par_iter()
            .map(|&r_val| r_val * r_val)
            .sum();

        // Avoid division by zero
        if rho0.abs() < 1e-30 {
            rho0 = 1e-30;
        }

        beta = rho / rho0;

        // Update p using mutable iterators
        p[range.clone()].par_iter_mut()
            .zip(&r[range.clone()])
            .for_each(|(p_val, &r_val)| {
                *p_val = r_val + beta * *p_val;
            });
    }

    // Calculate r using enumeration and chunks_mut
    r.par_chunks_mut(1)
        .enumerate()
        .for_each(|(j, r_slice)| {
            if j >= 1 && j <= (*lastrow - *firstrow) as usize {
                let mut d = 0.0;
                for k in rowstr[j]..rowstr[j + 1] {
                    let k = k as usize;
                    let cidx = colidx[k];
                    if cidx >= 0 && (cidx as usize) < z.len() {
                        d = d + a[k] * z[cidx as usize];
                    }
                }
                r_slice[0] = d;
            }
        });

    // Calculate sum in parallel
    sum = (0..=(*lastcol - *firstcol))
        .into_par_iter()
        .map(|j| {
            let j = j as usize;
            let diff = x[j] - r[j];
            diff * diff
        })
        .sum();

    if sum < 0.0 {
        sum = 0.0;
    }
    *rnorm = sum.sqrt();
}

fn icnvrt(x: &f64, ipwr2: &i32) -> i32 {
    ((*ipwr2 as f64) * (*x)).trunc() as i32
}

fn makea(n: &mut i32, nz: &mut i32, a: &mut Vec<f64>, colidx: &mut Vec<i32>, rowstr: &mut Vec<i32>, 
         firstrow: &i32, lastrow: &i32, firstcol: &i32, lastcol: &i32, 
         arow: &mut Vec<i32>, acol: &mut Vec<i32>, aelt: &mut Vec<f64>, 
         iv: &mut Vec<i32>, tran: &mut f64, amult: &f64) {
    let (mut nzv, mut nn1): (i32, i32);
    let mut ivc: Vec<i32> = vec![0; (NONZER + 1) as usize];
    let mut vc: Vec<f64> = vec![0.0; (NONZER + 1) as usize];

    nn1 = 1;

    loop {
        nn1 = 2 * nn1;
        if nn1 >= *n {
            break;
        }
    }

    // Use threading pools for non-dependent parts
    // This portion needs careful synchronization with Mutex/RefCell
    // because we have shared mutation of tran
    let mut tran_local = *tran;
    let mut local_arow = vec![0; n.to_owned() as usize];
    let mut local_acol = vec![0; (*n * (NONZER + 1)) as usize];
    let mut local_aelt = vec![0.0; (*n * (NONZER + 1)) as usize];
    
    // Sequential generation is safer for this part due to the dependencies
    for iouter in 0..*n {
        let mut nzv = NONZER;
        
        sprnvc(n, &mut nzv, &nn1, &mut vc, &mut ivc, &mut tran_local, amult);
        vecset(&mut vc, &mut ivc, &mut nzv, iouter + 1, 0.5);
        
        local_arow[iouter as usize] = nzv;
        
        for ivelt in 0..nzv {
            local_acol[(iouter * (NONZER + 1) + ivelt) as usize] = ivc[ivelt as usize] - 1;
            local_aelt[(iouter * (NONZER + 1) + ivelt) as usize] = vc[ivelt as usize];
        }
    }
    
    // Copy back the results
    *tran = tran_local;
    for i in 0..*n as usize {
        arow[i] = local_arow[i];
    }
    
    for i in 0..(*n * (NONZER + 1)) as usize {
        acol[i] = local_acol[i];
        aelt[i] = local_aelt[i];
    }

    sparse(a, colidx, rowstr, n, nz, arow, acol, aelt, firstrow, lastrow, iv);
}

fn sparse(a: &mut Vec<f64>, colidx: &mut Vec<i32>, rowstr: &mut Vec<i32>, n: &mut i32, nz: &mut i32, 
    arow: &mut Vec<i32>, acol: &mut Vec<i32>, aelt: &mut Vec<f64>, 
    firstrow: &i32, lastrow: &i32, nzloc: &mut Vec<i32>) {
    
    let nrows: i32 = *lastrow - *firstrow + 1;

    // Sequential initialization to avoid borrow issues
    for j in 0..=nrows {
        rowstr[j as usize] = 0;
    }

    // Initial count - fix to match sequential version
    for i in 0..*n {
        for nza in 0..arow[i as usize] {
            let local_j = acol[(i * (NONZER + 1) + nza) as usize] + 1;
            if local_j >= 0 && local_j <= nrows {
                // This is the key difference - add entire row count at once
                rowstr[local_j as usize] = rowstr[local_j as usize] + arow[i as usize];
            }
        }
    }

    // Set up rowstr
    rowstr[0] = 0;
    for j in 1..=nrows {
        rowstr[j as usize] = rowstr[j as usize] + rowstr[(j - 1) as usize];
    }

    // Check if space is exceeded
    let nza = rowstr[nrows as usize];
    if nza > *nz {
        println!("Space for matrix elements exceeded in sparse");
        println!("nza, nzmax = {}, {}", &nza, &nz);
        std::process::exit(-1);
    }

    // Initialize arrays
    for j in 0..nrows as usize {
        for k in rowstr[j]..rowstr[j+1] {
            let k = k as usize;
            a[k] = 0.0;
            colidx[k] = -1;
        }
        nzloc[j] = 0;
    }

    // Fill matrix - use algorithm from sequential version
    let mut size = 1.0;
    let ratio = f64::powf(RCOND, 1.0 / (*n as f64));

    for i in 0..*n {
        for nza in 0..arow[i as usize] {
            let j = acol[(i * (NONZER + 1) + nza) as usize];
            
            if j >= 0 && j < *n {
                let scale = size * aelt[(i * (NONZER + 1) + nza) as usize];
                
                for nzrow in 0..arow[i as usize] {
                    let jcol = acol[(i * (NONZER + 1) + nzrow) as usize];
                    
                    if jcol >= 0 && jcol < *n {
                        let va = aelt[(i * (NONZER + 1) + nzrow) as usize] * scale;
                        let mut value = va;
                        
                        // Add diagonal term
                        if jcol == j && j == i {
                            value = va + RCOND - SHIFT;
                        }
                        
                        // Find position in sparse matrix
                        let mut found = false;
                        let mut last_k = 0;
                        for k in rowstr[j as usize]..rowstr[(j + 1) as usize] {
                            last_k = k;
                            if colidx[k as usize] > jcol {
                                // Need to shift elements to make room
                                let mut kk = rowstr[(j + 1) as usize] - 2;
                                
                                while kk >= k {
                                    if colidx[kk as usize] > -1 {
                                        a[(kk + 1) as usize] = a[kk as usize];
                                        colidx[(kk + 1) as usize] = colidx[kk as usize];
                                    }
                                    kk -= 1;
                                }
                                
                                colidx[k as usize] = jcol;
                                a[k as usize] = 0.0;
                                found = true;
                                break;
                            } else if colidx[k as usize] == -1 {
                                colidx[k as usize] = jcol;
                                found = true;
                                break;
                            } else if colidx[k as usize] == jcol {
                                // Update count in nzloc
                                nzloc[j as usize] = nzloc[j as usize] + 1;
                                found = true;
                                break;
                            }
                        }
                        
                        if !found {
                            println!("Error in sparse matrix construction");
                            std::process::exit(-1);
                        }
                        
                        // Add value at the position found
                        a[last_k as usize] = a[last_k as usize] + value;
                    }
                }
            }
        }
        size = size * ratio;
    }

    // Reorder and compact the sparse matrix (this was missing in the parallel version)
    for j in 1..nrows {
        nzloc[j as usize] = nzloc[j as usize] + nzloc[(j - 1) as usize];
    }

    for j in 0..nrows {
        let j1: i32 = if j > 0 {
            rowstr[j as usize] - nzloc[(j - 1) as usize]
        } else {
            0
        };
        let j2: i32 = rowstr[(j + 1) as usize] - nzloc[j as usize];
        let mut nza: i32 = rowstr[j as usize];
        
        for k in j1..j2 {
            a[k as usize] = a[nza as usize];
            colidx[k as usize] = colidx[nza as usize];
            nza += 1;
        }
    }

    for j in 1..=nrows {
        rowstr[j as usize] = rowstr[j as usize] - nzloc[(j - 1) as usize];
    }

    // Add this as the last line of the sparse function
    let mut nza = rowstr[nrows as usize] - 1;
}

fn sprnvc(n: &mut i32, nz: &mut i32, nn1: &i32, v: &mut Vec<f64>, iv: &mut Vec<i32>, 
          tran: &mut f64, amult: &f64) {
    let mut nzv: i32 = 0;

    while nzv < *nz {
        let vecelt = randdp::randlc(tran, *amult);
        let vecloc = randdp::randlc(tran, *amult);
        let i = icnvrt(&vecloc, nn1) + 1;

        if i > *n {
            continue;
        }

        let was_gen = (0..nzv).any(|ii| iv[ii as usize] == i);
        if was_gen {
            continue;
        }

        v[nzv as usize] = vecelt;
        iv[nzv as usize] = i;
        nzv += 1;
    }
}

fn vecset(v: &mut Vec<f64>, iv: &mut Vec<i32>, nzv: &mut i32, i: i32, val: f64) {
    let set = (0..*nzv).any(|k| {
        if iv[k as usize] == i {
            v[k as usize] = val;
            true
        } else {
            false
        }
    });

    if !set {
        v[*nzv as usize] = val;
        iv[*nzv as usize] = i;
        *nzv += 1;
    }
}