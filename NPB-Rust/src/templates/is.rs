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
use rayon::prelude::*;

#[cfg(not(any(feature = "class_c", feature = "class_d")))]
type KeyType = i32;

#[cfg(any(feature = "class_c", feature = "class_d"))]
type KeyType = i64;

struct ISBenchmark {
    key_array: Vec<KeyType>,
    key_buff1: Vec<KeyType>,
    key_buff2: Vec<KeyType>,
    partial_verify_vals: Vec<KeyType>,
    bucket_size: Vec<Vec<KeyType>>,
    bucket_ptrs: Vec<KeyType>,
    test_index_array: [usize; TEST_ARRAY_SIZE],
    test_rank_array: [KeyType; TEST_ARRAY_SIZE],
    passed_verification: i32,
    num_threads: usize,
    key_buff_ptr_global: Vec<KeyType>,
}

impl ISBenchmark {
    fn new(num_threads: usize) -> Self {
        let mut benchmark = ISBenchmark {
            key_array: vec![0; TOTAL_KEYS],
            key_buff1: vec![0; MAX_KEY],
            key_buff2: vec![0; TOTAL_KEYS],
            partial_verify_vals: vec![0; TEST_ARRAY_SIZE],
            bucket_size: vec![vec![0; NUM_BUCKETS]; num_threads],
            bucket_ptrs: vec![0; NUM_BUCKETS + 1],
            test_index_array: [0; TEST_ARRAY_SIZE],
            test_rank_array: [0; TEST_ARRAY_SIZE],
            passed_verification: 0,
            num_threads,
            key_buff_ptr_global: vec![0; MAX_KEY],
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
        
        let key_chunks: Vec<(usize, Vec<KeyType>)> = (0..self.num_threads)
            .into_par_iter()
            .filter_map(|thread_id| {
                let start_idx = thread_id * chunk_size;
                let end_idx = (start_idx + chunk_size).min(TOTAL_KEYS);
                
                if start_idx >= TOTAL_KEYS {
                    return None;
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
                let mut chunk = Vec::with_capacity(end_idx - start_idx);
                
                for _ in start_idx..end_idx {
                    let mut x = randdp::randlc(&mut seed, 1220703125.0);
                    x += randdp::randlc(&mut seed, 1220703125.0);
                    x += randdp::randlc(&mut seed, 1220703125.0);
                    x += randdp::randlc(&mut seed, 1220703125.0);
                    
                    chunk.push((k * x) as KeyType);
                }
                
                Some((start_idx, chunk))
            })
            .collect();
        
        for (start_idx, chunk) in key_chunks {
            for (i, key) in chunk.into_iter().enumerate() {
                if start_idx + i < TOTAL_KEYS {
                    self.key_array[start_idx + i] = key;
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
        self.key_array[(iteration + MAX_ITERATIONS) as usize] = (MAX_KEY as KeyType) - (iteration as KeyType);
        
        for i in 0..TEST_ARRAY_SIZE {
            self.partial_verify_vals[i] = self.key_array[self.test_index_array[i]];
        }
        
        let shift = MAX_KEY_LOG_2 - NUM_BUCKETS_LOG_2;
        let num_bucket_keys = 1 << shift;
        
        for thread_bucket in &mut self.bucket_size {
            thread_bucket.fill(0);
        }
        
        let chunk_size = (TOTAL_KEYS + self.num_threads - 1) / self.num_threads;
        
        for (thread_id, chunk) in self.key_array.chunks(chunk_size).enumerate() {
            if thread_id < self.num_threads {
                for &key in chunk {
                    let bucket_idx = (key >> shift) as usize;
                    if bucket_idx < NUM_BUCKETS {
                        self.bucket_size[thread_id][bucket_idx] += 1;
                    }
                }
            }
        }
        
        self.bucket_ptrs[0] = 0;
        for i in 1..NUM_BUCKETS {
            self.bucket_ptrs[i] = self.bucket_ptrs[i-1];
            for k in 0..self.num_threads {
                self.bucket_ptrs[i] += self.bucket_size[k][i-1];
            }
        }
        
        let mut bucket_offsets = vec![vec![0; NUM_BUCKETS]; self.num_threads];
        for thread_id in 0..self.num_threads {
            for i in 0..NUM_BUCKETS {
                bucket_offsets[thread_id][i] = self.bucket_ptrs[i];
                for prev_thread in 0..thread_id {
                    bucket_offsets[thread_id][i] += self.bucket_size[prev_thread][i];
                }
            }
        }
        
        for (thread_id, chunk) in self.key_array.chunks(chunk_size).enumerate() {
            if thread_id < self.num_threads {
                for &key in chunk {
                    let bucket_idx = (key >> shift) as usize;
                    if bucket_idx < NUM_BUCKETS {
                        let pos = bucket_offsets[thread_id][bucket_idx] as usize;
                        if pos < TOTAL_KEYS {
                            self.key_buff2[pos] = key;
                            bucket_offsets[thread_id][bucket_idx] += 1;
                        }
                    }
                }
            }
        }
        
        for i in 0..NUM_BUCKETS {
            self.bucket_ptrs[i] = 0;
            for k in 0..self.num_threads {
                self.bucket_ptrs[i] += self.bucket_size[k][i];
            }
            if i > 0 {
                self.bucket_ptrs[i] += self.bucket_ptrs[i-1];
            }
        }
        self.bucket_ptrs[NUM_BUCKETS] = self.bucket_ptrs[NUM_BUCKETS - 1];
        
        self.key_buff1.fill(0);
        
        for i in 0..NUM_BUCKETS {
            let k1 = i * num_bucket_keys;
            let k2 = k1 + num_bucket_keys;
            
            for k in k1..k2.min(MAX_KEY) {
                self.key_buff1[k] = 0;
            }
            
            let m = if i > 0 { self.bucket_ptrs[i-1] } else { 0 };
            for k in m..self.bucket_ptrs[i] {
                let key = self.key_buff2[k as usize] as usize;
                if key < MAX_KEY {
                    self.key_buff1[key] += 1;
                }
            }
            
            if k1 < MAX_KEY {
                self.key_buff1[k1] += m;
                for k in (k1 + 1)..k2.min(MAX_KEY) {
                    self.key_buff1[k] += self.key_buff1[k-1];
                }
            }
        }
        
        self.partial_verify(iteration);
        
        if iteration == MAX_ITERATIONS {
            self.key_buff_ptr_global = self.key_buff1.clone();
        }
    }
    
    fn partial_verify(&mut self, iteration: i32) {
        for i in 0..TEST_ARRAY_SIZE {
            let k = self.partial_verify_vals[i];
            if k > 0 && k <= (TOTAL_KEYS as KeyType - 1) {
                let key_rank = if k > 0 && ((k - 1) as usize) < self.key_buff1.len() {
                    self.key_buff1[(k - 1) as usize]
                } else {
                    0
                };
                
                let mut failed = false;
                
                match CLASS {
                    "s" => {
                        if i <= 2 {
                            if key_rank != self.test_rank_array[i] + iteration as KeyType {
                                failed = true;
                            } else {
                                self.passed_verification += 1;
                            }
                        } else {
                            if key_rank != self.test_rank_array[i] - iteration as KeyType {
                                failed = true;
                            } else {
                                self.passed_verification += 1;
                            }
                        }
                    }
                    "w" => {
                        if i < 2 {
                            if key_rank != self.test_rank_array[i] + (iteration - 2) as KeyType {
                                failed = true;
                            } else {
                                self.passed_verification += 1;
                            }
                        } else {
                            if key_rank != self.test_rank_array[i] - iteration as KeyType {
                                failed = true;
                            } else {
                                self.passed_verification += 1;
                            }
                        }
                    }
                    "a" => {
                        if i <= 2 {
                            if key_rank != self.test_rank_array[i] + (iteration - 1) as KeyType {
                                failed = true;
                            } else {
                                self.passed_verification += 1;
                            }
                        } else {
                            if key_rank != self.test_rank_array[i] - (iteration - 1) as KeyType {
                                failed = true;
                            } else {
                                self.passed_verification += 1;
                            }
                        }
                    }
                    "b" => {
                        if i == 1 || i == 2 || i == 4 {
                            if key_rank != self.test_rank_array[i] + iteration as KeyType {
                                failed = true;
                            } else {
                                self.passed_verification += 1;
                            }
                        } else {
                            if key_rank != self.test_rank_array[i] - iteration as KeyType {
                                failed = true;
                            } else {
                                self.passed_verification += 1;
                            }
                        }
                    }
                    "c" => {
                        if i <= 2 {
                            if key_rank != self.test_rank_array[i] + iteration as KeyType {
                                failed = true;
                            } else {
                                self.passed_verification += 1;
                            }
                        } else {
                            if key_rank != self.test_rank_array[i] - iteration as KeyType {
                                failed = true;
                            } else {
                                self.passed_verification += 1;
                            }
                        }
                    }
                    "d" => {
                        if i < 2 {
                            if key_rank != self.test_rank_array[i] + iteration as KeyType {
                                failed = true;
                            } else {
                                self.passed_verification += 1;
                            }
                        } else {
                            if key_rank != self.test_rank_array[i] - iteration as KeyType {
                                failed = true;
                            } else {
                                self.passed_verification += 1;
                            }
                        }
                    }
                    _ => {}
                }
                
                if failed {
                    println!("Failed partial verification: iteration {}, test key {}", iteration, i);
                }
            }
        }
    }
    
    fn full_verify(&mut self) {
        for j in 0..NUM_BUCKETS {
            let k1 = if j > 0 { self.bucket_ptrs[j-1] } else { 0 };
            let k2 = self.bucket_ptrs[j];
            
            for i in k1..k2 {
                let key = self.key_buff2[i as usize];
                if key >= 0 && (key as usize) < MAX_KEY {
                    let key_idx = key as usize;
                    self.key_buff_ptr_global[key_idx] -= 1;
                    let k = self.key_buff_ptr_global[key_idx];
                    if k >= 0 && (k as usize) < TOTAL_KEYS {
                        self.key_array[k as usize] = key;
                    }
                }
            }
        }
        
        let mut error_count = 0;
        for i in 1..TOTAL_KEYS {
            if self.key_array[i-1] > self.key_array[i] {
                error_count += 1;
            }
        }
        
        if error_count != 0 {
            println!("Full_verify: number of keys out of sort: {}", error_count);
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
    
    if num_threads > 1 {
        rayon::ThreadPoolBuilder::new()
            .num_threads(num_threads)
            .build_global()
            .unwrap_or_else(|e| {
                eprintln!("Failed to set thread pool: {}", e);
            });
    }
    
    println!("\n\n NAS Parallel Benchmarks 4.1 Parallel Rust version - IS Benchmark\n");
    println!(" Size:  {}  (class {})", TOTAL_KEYS, CLASS.to_uppercase());
    println!(" Iterations:   {}", MAX_ITERATIONS);
    println!(" Number of threads: {}", num_threads);
    println!();
    
    let mut benchmark = ISBenchmark::new(num_threads);
    
    let init_timer = Instant::now();
    benchmark.create_seq();
    let init_time = init_timer.elapsed().as_secs_f64();
    
    benchmark.rank(1);
    
    benchmark.passed_verification = 0;
    
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