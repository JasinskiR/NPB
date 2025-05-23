const CLASS: &str = %% CLASS_NPB %%;
const TOTAL_KEYS_LOG_2: u32 = %% TOTAL_KEYS_LOG_2 %%;
const MAX_KEY_LOG_2: u32 = %% MAX_KEY_LOG_2 %%;
const NUM_BUCKETS_LOG_2: u32 = %% NUM_BUCKETS_LOG_2 %%;
const COMPILETIME: &str = %% COMPILE_TIME %%;
const NPBVERSION: &str = "4.1";
const COMPILERVERSION: &str = "rustc 1.70.0-nightly";
const LIBVERSION: &str = "1.0";

const TOTAL_KEYS: usize = 1 << TOTAL_KEYS_LOG_2;
const MAX_KEY: usize = 1 << MAX_KEY_LOG_2;
const NUM_BUCKETS: usize = 1 << NUM_BUCKETS_LOG_2;
const MAX_ITERATIONS: i32 = 10;
const TEST_ARRAY_SIZE: usize = 5;

use common::print_results;
use common::randdp;
use std::time::Instant;
use std::env;
use chrono::Local;
use rayon::prelude::*;
use std::sync::atomic::{AtomicI64, Ordering};

#[cfg(not(any(feature = "class_c", feature = "class_d")))]
type KeyType = i32;

#[cfg(any(feature = "class_c", feature = "class_d"))]
type KeyType = i64;

#[derive(Clone)]
struct ISBenchmark {
    key_array: Vec<KeyType>,
    key_buff1: Vec<KeyType>,
    key_buff2: Vec<KeyType>,
    partial_verify_vals: Vec<KeyType>,
    bucket_size: Vec<Vec<KeyType>>,
    bucket_ptrs: Vec<KeyType>,
    test_index_array: [KeyType; TEST_ARRAY_SIZE],
    test_rank_array: [KeyType; TEST_ARRAY_SIZE],
    passed_verification: i32,
    num_threads: usize,
}

impl ISBenchmark {
    fn new(num_threads: usize) -> Self {
        let mut benchmark = ISBenchmark {
            key_array: vec![0; TOTAL_KEYS],
            key_buff1: vec![0; MAX_KEY],
            key_buff2: vec![0; TOTAL_KEYS],
            partial_verify_vals: vec![0; TEST_ARRAY_SIZE],
            bucket_size: vec![vec![0; NUM_BUCKETS]; num_threads],
            bucket_ptrs: vec![0; NUM_BUCKETS],
            test_index_array: [0; TEST_ARRAY_SIZE],
            test_rank_array: [0; TEST_ARRAY_SIZE],
            passed_verification: 0,
            num_threads,
        };
        
        benchmark.initialize_verification_arrays();
        benchmark
    }
    
    fn initialize_verification_arrays(&mut self) {
        match CLASS {
            "s" => {
                self.test_index_array = [48427, 17148, 23627, 62548, 4431];
                self.test_rank_array = [0, 18, 346, 64917, 65463];
            }
            "w" => {
                self.test_index_array = [357773, 934767, 875723, 898999, 404505];
                self.test_rank_array = [1249, 11698, 1039987, 1043896, 1048018];
            }
            "a" => {
                self.test_index_array = [2112377, 662041, 5336171, 3642833, 4250760];
                self.test_rank_array = [104, 17523, 123928, 8288932, 8388264];
            }
            "b" => {
                self.test_index_array = [41869, 812306, 5102857, 18232239, 26860214];
                self.test_rank_array = [33422937, 10244, 59149, 33135281, 99];
            }
            "c" => {
                self.test_index_array = [44172927, 72999161, 74326391, 129606274, 21736814];
                self.test_rank_array = [61147, 882988, 266290, 133997595, 133525895];
            }
            "d" => {
                self.test_index_array = [1317351170, 995930646, 1157283250, 1503301535, 1453734525];
                self.test_rank_array = [1, 36538729, 1978098519, 2145192618, 2147425337];
            }
            _ => {}
        }
    }
    
    fn create_seq(&mut self) {
        let chunk_size = (TOTAL_KEYS + self.num_threads - 1) / self.num_threads;
        
        let mut key_chunks: Vec<Vec<KeyType>> = (0..self.num_threads)
            .into_par_iter()
            .map(|thread_id| {
                let start_idx = thread_id * chunk_size;
                let end_idx = (start_idx + chunk_size).min(TOTAL_KEYS);
                let actual_size = end_idx - start_idx;
                
                if actual_size == 0 {
                    return Vec::new();
                }
                
                let s = Self::find_my_seed(
                    thread_id as i32,
                    self.num_threads as i32,
                    (4 * TOTAL_KEYS) as i64,
                    314159265.0,
                    1220703125.0
                );
                
                let mut seed = s;
                let k = (MAX_KEY / 4) as f64;
                let mut chunk = vec![0; actual_size];
                
                for key in chunk.iter_mut() {
                    let mut x = randdp::randlc(&mut seed, 1220703125.0);
                    x += randdp::randlc(&mut seed, 1220703125.0);
                    x += randdp::randlc(&mut seed, 1220703125.0);
                    x += randdp::randlc(&mut seed, 1220703125.0);
                    
                    *key = (k * x) as KeyType;
                }
                
                chunk
            })
            .collect();
        
        let mut idx = 0;
        for chunk in key_chunks.drain(..) {
            for key in chunk {
                if idx < TOTAL_KEYS {
                    self.key_array[idx] = key;
                    idx += 1;
                }
            }
        }
    }
    
    fn find_my_seed(kn: i32, np: i32, nn: i64, s: f64, a: f64) -> f64 {
        if kn == 0 {
            return s;
        }
        
        let mq = (nn / 4 + np as i64 - 1) / np as i64;
        let nq = mq * 4 * kn as i64;
        
        let mut t1 = s;
        let mut t2 = a;
        let mut kk = nq;
        
        while kk > 1 {
            let ik = kk / 2;
            if 2 * ik == kk {
                let t2_copy = t2;
                randdp::randlc(&mut t2, t2_copy);
                kk = ik;
            } else {
                randdp::randlc(&mut t1, t2);
                kk = kk - 1;
            }
        }
        randdp::randlc(&mut t1, t2);
        
        t1
    }
    
    fn rank(&mut self, iteration: i32) {
        self.key_array[iteration as usize] = iteration as KeyType;
        self.key_array[(iteration + MAX_ITERATIONS) as usize] = MAX_KEY as KeyType - iteration as KeyType;
        
        for i in 0..TEST_ARRAY_SIZE {
            self.partial_verify_vals[i] = self.key_array[self.test_index_array[i] as usize];
        }
        
        let shift = MAX_KEY_LOG_2 - NUM_BUCKETS_LOG_2;
        let num_bucket_keys = 1 << shift;
        
        for thread_buckets in &mut self.bucket_size {
            thread_buckets.fill(0);
        }
        
        let chunk_size = (TOTAL_KEYS + self.num_threads - 1) / self.num_threads;
        let bucket_counts: Vec<Vec<KeyType>> = self.key_array
            .par_chunks(chunk_size)
            .map(|chunk| {
                let mut local_buckets = vec![0; NUM_BUCKETS];
                for &key in chunk {
                    let bucket_idx = (key >> shift) as usize;
                    if bucket_idx < NUM_BUCKETS {
                        local_buckets[bucket_idx] += 1;
                    }
                }
                local_buckets
            })
            .collect();
        
        for (thread_id, local_buckets) in bucket_counts.into_iter().enumerate() {
            if thread_id < self.num_threads {
                for (i, count) in local_buckets.into_iter().enumerate() {
                    self.bucket_size[thread_id][i] = count;
                }
            }
        }
        
        self.bucket_ptrs[0] = 0;
        for i in 1..NUM_BUCKETS {
            self.bucket_ptrs[i] = self.bucket_ptrs[i-1];
            for thread_id in 0..self.num_threads {
                self.bucket_ptrs[i] += self.bucket_size[thread_id][i-1];
            }
        }
        
        let mut thread_bucket_starts = vec![vec![0; NUM_BUCKETS]; self.num_threads];
        for thread_id in 0..self.num_threads {
            for i in 0..NUM_BUCKETS {
                thread_bucket_starts[thread_id][i] = self.bucket_ptrs[i];
                for prev_thread in 0..thread_id {
                    thread_bucket_starts[thread_id][i] += self.bucket_size[prev_thread][i];
                }
            }
        }
        
        let key_positions: Vec<Vec<(usize, KeyType)>> = self.key_array
            .par_chunks(chunk_size)
            .enumerate()
            .map(|(thread_id, chunk)| {
                let mut local_starts = if thread_id < thread_bucket_starts.len() {
                    thread_bucket_starts[thread_id].clone()
                } else {
                    vec![0; NUM_BUCKETS]
                };
                let mut local_keys = Vec::new();
                
                for &key in chunk {
                    let bucket_idx = (key >> shift) as usize;
                    if bucket_idx < NUM_BUCKETS && local_starts[bucket_idx] < TOTAL_KEYS as KeyType {
                        local_keys.push((local_starts[bucket_idx] as usize, key));
                        local_starts[bucket_idx] += 1;
                    }
                }
                local_keys
            })
            .collect();
        
        for local_keys in key_positions {
            for (pos, key) in local_keys {
                if pos < self.key_buff2.len() {
                    self.key_buff2[pos] = key;
                }
            }
        }
        
        for i in 0..NUM_BUCKETS {
            self.bucket_ptrs[i] = 0;
            for thread_id in 0..self.num_threads {
                self.bucket_ptrs[i] += self.bucket_size[thread_id][i];
            }
            if i > 0 {
                self.bucket_ptrs[i] += self.bucket_ptrs[i-1];
            }
        }
        
        self.key_buff1.par_iter_mut().for_each(|x| *x = 0);
        
        let atomic_counts: Vec<AtomicI64> = (0..MAX_KEY)
            .map(|_| AtomicI64::new(0))
            .collect();
        
        (0..NUM_BUCKETS).into_par_iter().for_each(|i| {
            let start = if i > 0 { self.bucket_ptrs[i-1] } else { 0 };
            let end = self.bucket_ptrs[i];
            
            for k in start..end {
                let key_idx = self.key_buff2[k as usize] as usize;
                if key_idx < MAX_KEY {
                    atomic_counts[key_idx].fetch_add(1, Ordering::Relaxed);
                }
            }
        });
        
        for (i, atomic_count) in atomic_counts.iter().enumerate() {
            self.key_buff1[i] = atomic_count.load(Ordering::Relaxed) as KeyType;
        }
        
        (0..NUM_BUCKETS).into_par_iter().for_each(|i| {
            let k1 = i * num_bucket_keys;
            let k2 = k1 + num_bucket_keys;
            let start = if i > 0 { self.bucket_ptrs[i-1] } else { 0 };
            
            if k1 < MAX_KEY {
                let current_val = atomic_counts[k1].load(Ordering::Relaxed);
                atomic_counts[k1].store(current_val + start as i64, Ordering::Relaxed);
                
                for k in (k1 + 1)..k2.min(MAX_KEY) {
                    let prev_val = atomic_counts[k - 1].load(Ordering::Relaxed);
                    let curr_val = atomic_counts[k].load(Ordering::Relaxed);
                    atomic_counts[k].store(curr_val + prev_val, Ordering::Relaxed);
                }
            }
        });
        
        for (i, atomic_count) in atomic_counts.iter().enumerate() {
            self.key_buff1[i] = atomic_count.load(Ordering::Relaxed) as KeyType;
        }
        
        self.partial_verify(iteration);
    }
    
    fn partial_verify(&mut self, iteration: i32) {
        for i in 0..TEST_ARRAY_SIZE {
            let k = self.partial_verify_vals[i];
            if k > 0 && k <= TOTAL_KEYS as KeyType - 1 {
                let key_rank = if k > 0 && ((k - 1) as usize) < self.key_buff1.len() {
                    self.key_buff1[(k - 1) as usize]
                } else {
                    0
                };
                let mut failed = false;
                
                match CLASS {
                    "s" => {
                        if i <= 2 {
                            failed = key_rank != self.test_rank_array[i] + iteration as KeyType;
                        } else {
                            failed = key_rank != self.test_rank_array[i] - iteration as KeyType;
                        }
                    }
                    "w" => {
                        if i < 2 {
                            failed = key_rank != self.test_rank_array[i] + (iteration - 2) as KeyType;
                        } else {
                            failed = key_rank != self.test_rank_array[i] - iteration as KeyType;
                        }
                    }
                    "a" => {
                        if i <= 2 {
                            failed = key_rank != self.test_rank_array[i] + (iteration - 1) as KeyType;
                        } else {
                            failed = key_rank != self.test_rank_array[i] - (iteration - 1) as KeyType;
                        }
                    }
                    "b" => {
                        if i == 1 || i == 2 || i == 4 {
                            failed = key_rank != self.test_rank_array[i] + iteration as KeyType;
                        } else {
                            failed = key_rank != self.test_rank_array[i] - iteration as KeyType;
                        }
                    }
                    "c" => {
                        if i <= 2 {
                            failed = key_rank != self.test_rank_array[i] + iteration as KeyType;
                        } else {
                            failed = key_rank != self.test_rank_array[i] - iteration as KeyType;
                        }
                    }
                    "d" => {
                        if i < 2 {
                            failed = key_rank != self.test_rank_array[i] + iteration as KeyType;
                        } else {
                            failed = key_rank != self.test_rank_array[i] - iteration as KeyType;
                        }
                    }
                    _ => {}
                }
                
                if !failed {
                    self.passed_verification += 1;
                }
            }
        }
    }
    
    fn full_verify(&mut self) {
        let atomic_counts: Vec<AtomicI64> = (0..MAX_KEY)
            .map(|i| AtomicI64::new(self.key_buff1[i] as i64))
            .collect();
        
        let sorted_keys: Vec<Vec<(usize, KeyType)>> = (0..NUM_BUCKETS)
            .into_par_iter()
            .map(|j| {
                let k1 = if j > 0 { self.bucket_ptrs[j-1] } else { 0 };
                let k2 = self.bucket_ptrs[j];
                let mut local_sorted = Vec::new();
                
                for i in k1..k2 {
                    let key = self.key_buff2[i as usize];
                    if key > 0 && (key as usize) < MAX_KEY {
                        let new_pos = atomic_counts[key as usize].fetch_sub(1, Ordering::Relaxed) - 1;
                        if new_pos >= 0 && (new_pos as usize) < TOTAL_KEYS {
                            local_sorted.push((new_pos as usize, key));
                        }
                    }
                }
                local_sorted
            })
            .collect();
        
        for bucket_keys in sorted_keys {
            for (pos, key) in bucket_keys {
                if pos < self.key_array.len() {
                    self.key_array[pos] = key;
                }
            }
        }
        
        let errors = self.key_array
            .par_windows(2)
            .map(|pair| if pair[0] > pair[1] { 1 } else { 0 })
            .sum::<i32>();
        
        if errors != 0 {
            println!("Full_verify: number of keys out of sort: {}", errors);
        } else {
            self.passed_verification += 1;
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let num_threads = if args.len() > 1 {
        args[1].parse::<usize>().unwrap_or(1)
    } else {
        1
    };
    
    rayon::ThreadPoolBuilder::new()
        .num_threads(num_threads)
        .build_global()
        .unwrap();
    
    println!("\n\n NAS Parallel Benchmarks 4.1 Parallel Rust version - IS Benchmark\n");
    println!(" Size:  {}  (class {})", TOTAL_KEYS, CLASS.to_uppercase());
    println!(" Iterations:   {}", MAX_ITERATIONS);
    println!(" Number of threads: {}", num_threads);
    println!();
    
    let mut benchmark = ISBenchmark::new(num_threads);
    
    let init_timer = Instant::now();
    benchmark.create_seq();
    let init_time = init_timer.elapsed().as_secs_f64();
    println!(" Initialization time = {:.6} seconds", init_time);
    
    benchmark.rank(1);
    
    if CLASS != "s" {
        println!("\n   iteration");
    }
    
    let bench_timer = Instant::now();
    
    for iteration in 1..=MAX_ITERATIONS {
        if CLASS != "s" {
            println!("        {}", iteration);
        }
        benchmark.rank(iteration);
    }
    
    let timecounter = bench_timer.elapsed().as_secs_f64();
    
    benchmark.full_verify();
    
    let verified = benchmark.passed_verification == 5 * MAX_ITERATIONS + 1;
    let mops = (MAX_ITERATIONS as f64 * TOTAL_KEYS as f64) / timecounter / 1000000.0;
    
    print_results::rust_print_results(
        "IS",
        CLASS,
        (TOTAL_KEYS / 64) as u32,
        64,
        0,
        MAX_ITERATIONS,
        timecounter,
        mops,
        "keys ranked",
        verified,
        NPBVERSION,
        COMPILETIME,
        COMPILERVERSION,
        LIBVERSION,
        &num_threads.to_string(),
        "",
        "",
        "",
        "",
        "",
        "",
        ""
    );
}