//NPB SET_PARAMS GLOBAL VARIABLES
// const CLASS: &str= %% CLASS_NPB %%; // Will be determined at runtime
// const M: u32 = %% M %%; // Will be determined at runtime
// const MM: u32 = M - MK; // Will be calculated at runtime
// const NN: u32 = 1 << MM; // Will be calculated at runtime
const COMPILETIME: &str = %% COMPILE_TIME %%;
const NPBVERSION: &str = "4.1";
const COMPILERVERSION: &str = "13.0.0";
const LIBVERSION: &str = "1.0";

//EP GLOBAL VARIABLES
const MK: u32 = 16;
const NK: usize = 1 << MK; // 2**MK
const EPSILON: f64 = 1.0e-8;
const A: f64 = 1220703125.0;
const S: f64 = 271828183.0;
const NQ: u32 = 10;
const NK_PLUS: usize = (2 * NK) + 1;

//IMPORTS
use common::randdp;
use common::print_results;
use std::time::Instant;
use std::mem::MaybeUninit;
use std::ptr;
use rayon::prelude::*;
use std::env;
use chrono::{Local, DateTime};
use std::cell::RefCell;
use std::thread_local;
use std::sync::atomic::{AtomicUsize, Ordering};

// Use thread_local! for the large temporary buffer
thread_local! {
    static THREAD_X: RefCell<Vec<f64>> = RefCell::new(vec![0.0; NK_PLUS]);
}

//BEGINNING OF EP
fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() < 3 {
        eprintln!("Usage: {} <CLASS> <NUM_THREADS>", args.get(0).map_or("ep-pp", |s| s.as_str()));
        eprintln!("Example: {} S 4", args.get(0).map_or("ep-pp", |s| s.as_str()));
        eprintln!("Available classes: S, W, A, B, C, D, E");
        std::process::exit(1);
    }
    
    let class_arg = &args[1];
    let class_npb: &str = class_arg.as_str();

    let m_val: u32 = match class_npb.to_uppercase().as_str() {
        "S" => 24,
        "W" => 25,
        "A" => 28,
        "B" => 30,
        "C" => 32,
        "D" => 36,
        "E" => 40,
        _ => {
            eprintln!("Invalid class: {}. Must be one of S, W, A, B, C, D, E.", class_npb);
            std::process::exit(1);
        }
    };

    let mm_val: u32 = m_val - MK;
    let nn_val: u32 = 1 << mm_val;
    
    // Get thread count from third argument (args[2])
    let num_threads: usize = args.get(2)
        .map(|s| s.parse::<usize>().unwrap_or_else(|e| {
            eprintln!("Invalid thread count '{}', using default: 1. Error: {}", s, e);
            1
        }))
        .unwrap_or_else(|| {
            // This path should not be hit due to args.len() < 3 check, but as a fallback:
            eprintln!("No thread count specified, using default: 1");
            1
        });

    println!(" NAS Parallel Benchmarks 4.1 Rust version - EP Benchmark");
    println!(" Class = {}, M = {}", class_npb.to_uppercase(), m_val);
    println!(" Number of random numbers generated: {}", nn_val);
    println!(" Using {} threads", num_threads);

    if let Err(e) = rayon::ThreadPoolBuilder::new()
        .num_threads(num_threads)
        .build_global() {
        eprintln!("Failed to build thread pool: {}, using default configuration", e);
    }

    // Integer Variables
    let np: i32 = nn_val as i32; // Use nn_val
    // Double Variables
    let mut aux: f64;
    let mut t1: f64;
    let ( sx, sy,tm, an, mut gc): (f64, f64, f64, f64, f64);
    let (mut sx_verify_value,mut sy_verify_value): (f64, f64);
    sx_verify_value = -1.0e99; //added because of the error: used binding `sx_verify_value` is possibly-uninitialized
    sy_verify_value = -1.0e99; //added because of the error: used binding `sy_verify_value` is possibly-uninitialized
    let (sx_err, sy_err): (f64, f64);

    // Boolean Variables
    let mut verified: bool;
    //let timers_enabled: bool = false;

    
    let mut x: Vec<f64> = Vec::with_capacity(NK_PLUS);
    let q: [f64;NQ as usize] = [0.0;NQ as usize];
    let mut dum0 = 1.0;
    let mut dum1 = 1.0;
    let mut dum2: Vec<f64> = Vec::with_capacity(1);
    
    dum2.push(1.0);
    randdp::vranlc(0, &mut dum0, dum1, &mut dum2);
    let dum3 = 1.0;
    let _dum0:f64 = randdp::randlc(&mut dum1, dum3);
    unsafe {
        let ptr = x.as_mut_ptr();
        ptr::write_bytes(ptr, 0xFF, NK_PLUS); // initializes the vector to all 1s
        let default_value = MaybeUninit::new(-1.0e99);
        for i in 0..NK_PLUS {
            ptr::write(ptr.offset(i as isize), default_value.assume_init());
        }
        x.set_len(NK_PLUS);
    }

    let start = Instant::now();
    t1 = A;
    randdp::vranlc(0, &mut t1, A, &mut x);

    t1 = A;

    for _ in 0..(MK + 1) {
        aux = t1;
        let _t2 = randdp::randlc(&mut t1, aux);
    }

    an = t1;
    gc = 0.0;

    // Dynamically determine chunk size based on problem size and thread count
    let chunk_size = ((np as usize) / (num_threads * 4)).max(1);

    // Use atomic counters for the histogram array to minimize synchronization
    let atomic_counts = (0..NQ as usize)
        .map(|_| AtomicUsize::new(0))
        .collect::<Vec<_>>();

    let result = (1..np+1)
        .collect::<Vec<_>>()
        .par_chunks(chunk_size)
        .fold(|| (0.0, 0.0), |mut acc, chunk| {
            for &k in chunk {
                let mut t1 = S;
                let mut t2 = an;
                let mut t3: f64;
                let mut t4: f64;
                let mut ik: i32;
                let mut l: usize; // Add this type annotation
                let k_offset = -1;
                let mut kk = k_offset + k;
                let mut aux: f64;
                for _i in 1..=100 {
                        ik = kk / 2;
                        if (2 * ik) != kk {
                            t3 = randdp::randlc(&mut t1, t2);
                        }
                        if ik == 0 {
                            break;
                        }
                        aux = t2;
                        t3 = randdp::randlc(&mut t2, aux);
                        kk = ik;
                    }
                THREAD_X.with(|x_cell| {
                    let mut x = x_cell.borrow_mut();
                    randdp::vranlc((2 * NK) as i32, &mut t1, A, &mut x);
                    
                    // Increase chunk size for better vectorization potential
                    const CHUNK_SIZE: usize = 128; 
                    for chunk_start in (0..NK).step_by(CHUNK_SIZE) {
                        let chunk_end = (chunk_start + CHUNK_SIZE).min(NK);
                        
                        // Pre-allocate variables to help compiler optimize
                        let mut sum_x = 0.0;
                        let mut sum_y = 0.0;
                        let mut local_counts = [0usize; NQ as usize];
                        
                        for i in chunk_start..chunk_end {
                            let x1 = 2.0 * x[2 * i] - 1.0;
                            let x2 = 2.0 * x[2 * i + 1] - 1.0;
                            let t1 = x1 * x1 + x2 * x2;
                            
                            if t1 <= 1.0 {
                                let t2 = (-2.0 * t1.ln() / t1).sqrt();
                                let t3 = x1 * t2;
                                let t4 = x2 * t2;
                                let l = t3.abs().max(t4.abs()) as usize;
                                
                                if l < NQ as usize {
                                    local_counts[l] += 1;
                                    sum_x += t3;
                                    sum_y += t4;
                                }
                            }
                        }
                        
                        // Accumulate results
                        acc.0 += sum_x;
                        acc.1 += sum_y;
                        
                        // Update atomic counters only once per chunk
                        for (i, count) in local_counts.iter().enumerate() {
                            if *count > 0 && i < atomic_counts.len() {
                                atomic_counts[i].fetch_add(*count, Ordering::Relaxed);
                            }
                        }
                    }
                });
            }
            acc
        })
        .reduce(|| (0.0, 0.0), |mut acc1, acc2| {
            acc1.0 += acc2.0;
            acc1.1 += acc2.1;
            acc1
        });

    // Convert atomic counts to a regular vector
    let counts = atomic_counts.iter()
        .map(|atomic| atomic.load(Ordering::Relaxed))
        .collect::<Vec<_>>();

    sx = result.0;
    sy = result.1;

    for item in counts.iter().take((NQ-1) as usize + 1){
        gc += *item as f64;
    }

    tm = start.elapsed().as_secs_f64();

    let nit = 0;
    verified = true;

    if m_val == 24 { // Use m_val
        sx_verify_value = -3.247_834_652_034_74e3;
        sy_verify_value = -6.958_407_078_382_297e3;
    }else if m_val == 25 { // Use m_val
        sx_verify_value = -2.863_319_731_645_753e3;
        sy_verify_value = -6.320_053_679_109_499e3;
    }else if m_val == 28 { // Use m_val
        sx_verify_value = -4.295_875_165_629_892e3;
        sy_verify_value = -1.580_732_573_678_431e4;
    }else if m_val == 30 { // Use m_val
        sx_verify_value =  4.033_815_542_441_498e4;
        sy_verify_value = -2.660_669_192_809_235e4;
    }else if m_val == 32 { // Use m_val
        sx_verify_value =  4.764_367_927_995_374e4;
        sy_verify_value = -8.084_072_988_043_731e4;
    }else if m_val == 36 { // Use m_val
        sx_verify_value =  1.982_481_200_946_593e5;
        sy_verify_value = -1.020_596_636_361_769e5;
    }else if  m_val == 40 { // Use m_val
        sx_verify_value = -5.319_717_441_530e5;
        sy_verify_value = -3.688_834_557_731e5;
    }else {
        verified = false; // Should not happen if class validation is correct
    }

    if verified {
        sx_err = ((sx - sx_verify_value) / sx_verify_value).abs();
        sy_err = ((sy - sy_verify_value) / sy_verify_value).abs();
        verified = (sx_err <= EPSILON) && (sy_err <= EPSILON);
    }
    else{
        println!("Something is wrong here!");
    }

    let mops: f64 = (((1 as i64) << ((m_val as i64) + 1)) as f64) / tm / 1000000.0; // Use m_val

    // Get current date and time for benchmark report
    let now: DateTime<Local> = Local::now();
    
    println!("\n EP Benchmark Results:\n");
    println!(" Run on: {}", now.format("%Y-%m-%d %H:%M:%S"));
    println!(" CPU Time = {:.6} seconds", tm);
    println!(" N = 2^{}", m_val); // Use m_val
    println!(" No. Gaussian Pairs = {:>15}", gc);
    println!(" Sums: sx = {:25.15e} sy = {:25.15e}", sx, sy); // %25.15e
    println!(" Counts: ");
    for i in 0..(NQ) as usize{  // Modified to include all counts
        println!("{}     {}",i,counts[i]);
    }
    print_results::rust_print_results("EP",
                        class_npb.to_uppercase().as_str(), // Use class_npb
                        m_val + 1, // Use m_val
                        0,
                        0,
                        nit,
                        tm,
                        mops,
                        "Random numbers generated",
                        verified,
                        NPBVERSION,
                        COMPILETIME,
                        COMPILERVERSION,
                        LIBVERSION,
                        format!("{}", num_threads).as_str(), // Pass the actual thread count
                        "cs1",
                        "cs2",
                        "cs3",
                        "cs4",
                        "cs5",
                        "cs6",
                        "cs7");
}
