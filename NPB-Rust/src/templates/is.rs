const COMPILETIME: &str = %% COMPILE_TIME %%;
const NPBVERSION: &str = "4.1";
const COMPILERVERSION: &str = "rustc 1.70.0-nightly";
const LIBVERSION: &str = "1.0";

const MAX_ITERATIONS: i32 = 10;
const TEST_ARRAY_SIZE: usize = 5;

use common::print_results;
use common::randdp;
use std::time::Instant;
use std::env;
use rayon::prelude::*;
use std::sync::atomic::{AtomicI64, Ordering};

type KeyType = i64;

#[repr(align(64))]
struct CacheAligned<T> {
    data: T,
}

struct ISBenchmark {
    class_char: String,
    total_keys: usize,
    max_key: usize,
    num_buckets: usize,
    max_key_log_2: u32,
    num_buckets_log_2: u32,

    key_array: Vec<KeyType>,
    key_buff1: Vec<KeyType>,
    key_buff2: Vec<KeyType>,
    partial_verify_vals: Vec<KeyType>,
    bucket_size: Vec<CacheAligned<Vec<KeyType>>>,
    bucket_ptrs: Vec<KeyType>,
    test_index_array: [usize; TEST_ARRAY_SIZE],
    test_rank_array: [KeyType; TEST_ARRAY_SIZE],
    passed_verification: i32,
    num_threads: usize,
    key_buff_ptr_global: Vec<KeyType>,
}

impl ISBenchmark {
    fn new(
        num_threads: usize,
        class_char_str: &str,
        total_keys_val: usize,
        max_key_val: usize,
        num_buckets_val: usize,
        max_key_log_2_val: u32,
        num_buckets_log_2_val: u32,
    ) -> Self {
        let bucket_size = (0..num_threads)
            .map(|_| CacheAligned {
                data: vec![0; num_buckets_val],
            })
            .collect();
        
        let mut benchmark = ISBenchmark {
            class_char: class_char_str.to_uppercase(),
            total_keys: total_keys_val,
            max_key: max_key_val,
            num_buckets: num_buckets_val,
            max_key_log_2: max_key_log_2_val,
            num_buckets_log_2: num_buckets_log_2_val,

            key_array: vec![0; total_keys_val],
            key_buff1: vec![0; max_key_val],
            key_buff2: vec![0; total_keys_val],
            partial_verify_vals: vec![0; TEST_ARRAY_SIZE],
            bucket_size,
            bucket_ptrs: vec![0; num_buckets_val + 1],
            test_index_array: [0; TEST_ARRAY_SIZE],
            test_rank_array: [0; TEST_ARRAY_SIZE],
            passed_verification: 0,
            num_threads,
            key_buff_ptr_global: vec![0; max_key_val],
        };
        
        benchmark.initialize_verification_arrays();
        benchmark
    }
    
    fn initialize_verification_arrays(&mut self) {
        match self.class_char.as_str() {
            "S" => {
                self.test_index_array = [48427, 17148, 23627, 62548, 4431];
                self.test_rank_array = [0, 18, 346, 64917, 65463];
            }
            "W" => {
                self.test_index_array = [357773, 934767, 875723, 898999, 404505];
                self.test_rank_array = [1249, 11698, 1039987, 1043896, 1048018];
            }
            "A" => {
                self.test_index_array = [2112377, 662041, 5336171, 3642833, 4250760];
                self.test_rank_array = [104, 17523, 123928, 8288932, 8388264];
            }
            "B" => {
                self.test_index_array = [41869, 812306, 5102857, 18232239, 26860214];
                self.test_rank_array = [33422937, 10244, 59149, 33135281, 99];
            }
            "C" => {
                self.test_index_array = [44172927, 72999161, 74326391, 129606274, 21736814];
                self.test_rank_array = [61147, 882988, 266290, 133997595, 133525895];
            }
            "D" => {
                self.test_index_array = [1317351170, 995930646, 1157283250, 1503301535, 1453734525];
                self.test_rank_array = [1, 36538729, 1978098519, 2145192618, 2147425337];
            }
            _ => {
                eprintln!("Warning: Unknown class '{}' for verification arrays.", self.class_char);
            }
        }
    }
    
    fn create_seq(&mut self) {
        let chunk_size = (self.total_keys + self.num_threads - 1) / self.num_threads;
        
        let key_chunks: Vec<(usize, Vec<KeyType>)> = (0..self.num_threads)
            .into_par_iter()
            .filter_map(|thread_id| {
                let start_idx = thread_id * chunk_size;
                let end_idx = (start_idx + chunk_size).min(self.total_keys);
                
                if start_idx >= self.total_keys {
                    return None;
                }
                
                let s = Self::find_my_seed(
                    thread_id as i32,
                    self.num_threads as i32,
                    (4 * self.total_keys) as i64,
                    314159265.0,
                    1220703125.0
                );
                
                let mut seed = s;
                let k = (self.max_key / 4) as f64;
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
                if start_idx + i < self.total_keys {
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
        self.key_array[(iteration + MAX_ITERATIONS) as usize] = (self.max_key as KeyType) - iteration as KeyType;
        
        for i in 0..TEST_ARRAY_SIZE {
            self.partial_verify_vals[i] = self.key_array[self.test_index_array[i]];
        }
        
        let shift = self.max_key_log_2 - self.num_buckets_log_2;
        let num_bucket_keys = 1 << shift;
        
        for thread_bucket in &mut self.bucket_size {
            thread_bucket.data.fill(0);
        }
        
        let chunk_size = (self.total_keys + self.num_threads - 1) / self.num_threads;
        
        for (thread_id, chunk) in self.key_array.chunks(chunk_size).enumerate() {
            if thread_id < self.num_threads {
                for &key in chunk {
                    let bucket_idx = (key >> shift) as usize;
                    if bucket_idx < self.num_buckets {
                        self.bucket_size[thread_id].data[bucket_idx] += 1;
                    }
                }
            }
        }
        
        self.bucket_ptrs[0] = 0;
        for i in 1..self.num_buckets {
            self.bucket_ptrs[i] = self.bucket_ptrs[i-1];
            for k in 0..self.num_threads {
                self.bucket_ptrs[i] += self.bucket_size[k].data[i-1];
            }
        }
        
        let mut bucket_offsets = vec![vec![0; self.num_buckets]; self.num_threads];
        for thread_id in 0..self.num_threads {
            for i in 0..self.num_buckets {
                bucket_offsets[thread_id][i] = self.bucket_ptrs[i];
                for prev_thread in 0..thread_id {
                    bucket_offsets[thread_id][i] += self.bucket_size[prev_thread].data[i];
                }
            }
        }
        
        for (thread_id, chunk) in self.key_array.chunks(chunk_size).enumerate() {
            if thread_id < self.num_threads {
                for &key in chunk {
                    let bucket_idx = (key >> shift) as usize;
                    if bucket_idx < self.num_buckets {
                        let pos = bucket_offsets[thread_id][bucket_idx] as usize;
                        if pos < self.total_keys {
                            self.key_buff2[pos] = key;
                            bucket_offsets[thread_id][bucket_idx] += 1;
                        }
                    }
                }
            }
        }
        
        for i in 0..self.num_buckets {
            self.bucket_ptrs[i] = 0;
            for k in 0..self.num_threads {
                self.bucket_ptrs[i] += self.bucket_size[k].data[i];
            }
            if i > 0 {
                self.bucket_ptrs[i] += self.bucket_ptrs[i-1];
            }
        }
        self.bucket_ptrs[self.num_buckets] = self.bucket_ptrs[self.num_buckets - 1];
        
        let start_indices: Vec<KeyType> = (0..self.num_buckets)
            .map(|i| if i == 0 { 0 } else { self.bucket_ptrs[i - 1] })
            .collect();
        
        let bucket_results: Vec<Vec<KeyType>> = (0..self.num_buckets)
            .into_par_iter()
            .map(|i| {
                let k1 = i * num_bucket_keys;
                let k2 = (k1 + num_bucket_keys).min(self.max_key);
                let segment_len = k2 - k1;
                
                let mut segment = vec![0; segment_len];
                
                let start_ptr = if i > 0 { self.bucket_ptrs[i - 1] } else { 0 };
                let end_ptr = self.bucket_ptrs[i];
                
                for j in start_ptr..end_ptr {
                    let key = self.key_buff2[j as usize] as usize;
                    if key >= k1 && key < k2 {
                        segment[key - k1] += 1;
                    }
                }
                
                if !segment.is_empty() {
                    segment[0] += start_indices[i];
                }
                
                for idx in 1..segment_len {
                    segment[idx] += segment[idx - 1];
                }
                
                segment
            })
            .collect();
    
        for (i, segment) in bucket_results.into_iter().enumerate() {
            let k1 = i * num_bucket_keys;
            let k2 = (k1 + num_bucket_keys).min(self.max_key);
            
            for (idx, &value) in segment.iter().enumerate() {
                if k1 + idx < k2 {
                    self.key_buff1[k1 + idx] = value;
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
            if k > 0 && k <= (self.total_keys as KeyType - 1) {
                let key_rank = if k > 0 && ((k - 1) as usize) < self.key_buff1.len() {
                    self.key_buff1[(k - 1) as usize]
                } else {
                    0
                };
                
                let mut failed = false;
                
                match self.class_char.as_str() {
                    "S" => {
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
                    "W" => {
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
                    "A" => {
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
                    "B" => {
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
                    "C" => {
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
                    "D" => {
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
        for j in 0..self.num_buckets {
            let k1 = if j > 0 { self.bucket_ptrs[j-1] } else { 0 };
            let k2 = self.bucket_ptrs[j];
            
            for i_val in k1..k2 {
                let key = self.key_buff2[i_val as usize];
                if key >= 0 && (key as usize) < self.max_key {
                    let key_idx = key as usize;
                    self.key_buff_ptr_global[key_idx] -= 1;
                    let k_final_pos = self.key_buff_ptr_global[key_idx];
                    if k_final_pos >= 0 && (k_final_pos as usize) < self.total_keys {
                        self.key_array[k_final_pos as usize] = key;
                    }
                }
            }
        }
        
        let mut error_count = 0;
        for i in 1..self.total_keys {
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

    if args.len() < 3 {
        eprintln!("Usage: {} <CLASS> <NUM_THREADS>", args.get(0).map_or("is", |s| s.as_str()));
        eprintln!("Example: {} S 4", args.get(0).map_or("is", |s| s.as_str()));
        eprintln!("Available classes: S, W, A, B, C, D");
        std::process::exit(1);
    }

    let class_arg = &args[1];
    let class_npb_val: String = class_arg.to_uppercase();

    let num_threads: usize = args[2].parse::<usize>().unwrap_or_else(|e| {
        eprintln!("Invalid thread count '{}', using default: 1. Error: {}", args[2], e);
        1
    });

    let (total_keys_log_2_val, max_key_log_2_val, num_buckets_log_2_val) = 
        match class_npb_val.as_str() {
            "S" => (16, 11, 9),
            "W" => (20, 16, 10),
            "A" => (23, 19, 10),
            "B" => (25, 21, 10),
            "C" => (27, 23, 10),
            "D" => (31, 27, 10),
            _ => {
                eprintln!("Invalid class: {}. Must be one of S, W, A, B, C, D.", class_npb_val);
                std::process::exit(1);
            }
    };

    let total_keys_val: usize = 1 << total_keys_log_2_val;
    let max_key_val: usize = 1 << max_key_log_2_val;
    let num_buckets_val: usize = 1 << num_buckets_log_2_val;
    
    if num_threads > 1 {
        if let Err(e) = rayon::ThreadPoolBuilder::new()
            .num_threads(num_threads)
            .build_global() {
                eprintln!("Failed to set thread pool: {}, using default Rayon configuration.", e);
            }
    }
    
    println!("\n\n NAS Parallel Benchmarks 4.1 Parallel Rust version - IS Benchmark\n");
    println!(" Size:  {}  (class {})", total_keys_val, class_npb_val);
    println!(" Iterations:   {}", MAX_ITERATIONS);
    println!(" Number of threads: {}", num_threads);
    println!();
    
    let mut benchmark = ISBenchmark::new(
        num_threads,
        &class_npb_val,
        total_keys_val,
        max_key_val,
        num_buckets_val,
        max_key_log_2_val,
        num_buckets_log_2_val,
    );
    
    let init_timer = Instant::now();
    benchmark.create_seq();
    let _init_time = init_timer.elapsed().as_secs_f64();
    
    benchmark.rank(1);
    
    benchmark.passed_verification = 0;
    
    if class_npb_val != "S" {
        println!("\n   iteration");
    }
    
    let bench_timer = Instant::now();
    
    for iteration in 1..=MAX_ITERATIONS {
        if class_npb_val != "S" {
            println!("        {}", iteration);
        }
        benchmark.rank(iteration);
    }
    
    let timecounter = bench_timer.elapsed().as_secs_f64();
    
    benchmark.full_verify();
    
    let verified = benchmark.passed_verification == (TEST_ARRAY_SIZE as i32 * MAX_ITERATIONS) + 1;
    let mops = if timecounter > 0.0 {
        (MAX_ITERATIONS as f64 * total_keys_val as f64) / timecounter / 1_000_000.0
    } else {
        0.0
    };
    
    print_results::rust_print_results(
        "IS",
        &class_npb_val,
        (total_keys_val / 64) as u32,
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
