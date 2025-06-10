use std::sync::{Arc, Mutex, mpsc};
use std::thread;
use std::time::{Duration, Instant};
use std::sync::atomic::{AtomicUsize, AtomicU64, AtomicBool, Ordering};
use std::collections::{VecDeque, hash_map::DefaultHasher};
use std::hash::{Hash, Hasher};
use std::env;
use std::fs;

#[derive(Debug)]
struct MemoryStats {
    peak_rss_kb: u64,
    current_rss_kb: u64,
    heap_size_estimated_kb: u64,
    thread_overhead_kb: u64,
}

impl MemoryStats {
    fn new() -> Self {
        Self {
            peak_rss_kb: 0,
            current_rss_kb: 0,
            heap_size_estimated_kb: 0,
            thread_overhead_kb: 0,
        }
    }
    
    fn measure_current(&mut self) {
        if let Ok(status) = fs::read_to_string("/proc/self/status") {
            for line in status.lines() {
                if line.starts_with("VmRSS:") {
                    if let Some(kb_str) = line.split_whitespace().nth(1) {
                        if let Ok(kb) = kb_str.parse::<u64>() {
                            self.current_rss_kb = kb;
                            self.peak_rss_kb = self.peak_rss_kb.max(kb);
                        }
                    }
                } else if line.starts_with("VmPeak:") {
                    if let Some(kb_str) = line.split_whitespace().nth(1) {
                        if let Ok(kb) = kb_str.parse::<u64>() {
                            self.peak_rss_kb = self.peak_rss_kb.max(kb);
                        }
                    }
                }
            }
        } else if cfg!(target_os = "macos") {
            self.measure_macos();
        } else if cfg!(target_os = "windows") {
            // Only call measure_windows if we're on Windows
            #[cfg(target_os = "windows")]
            self.measure_windows();
        }
    }
    
    #[cfg(target_os = "macos")]
    fn measure_macos(&mut self) {
        use std::process::Command;
        
        if let Ok(output) = Command::new("ps")
            .args(&["-o", "rss=", "-p"])
            .arg(std::process::id().to_string())
            .output() {
            if let Ok(rss_str) = String::from_utf8(output.stdout) {
                if let Ok(rss_kb) = rss_str.trim().parse::<u64>() {
                    self.current_rss_kb = rss_kb;
                    self.peak_rss_kb = self.peak_rss_kb.max(rss_kb);
                }
            }
        }
    }
    
    #[cfg(target_os = "windows")]
    fn measure_windows(&mut self) {
        use std::process::Command;
        
        if let Ok(output) = Command::new("tasklist")
            .args(&["/fi", &format!("PID eq {}", std::process::id()), "/fo", "csv"])
            .output() {
            if let Ok(output_str) = String::from_utf8(output.stdout) {
                if let Some(line) = output_str.lines().nth(1) {
                    let fields: Vec<&str> = line.split(',').collect();
                    if fields.len() > 4 {
                        let mem_str = fields[4].trim_matches('"').replace(",", "");
                        if let Some(kb_str) = mem_str.strip_suffix(" K") {
                            if let Ok(kb) = kb_str.parse::<u64>() {
                                self.current_rss_kb = kb;
                                self.peak_rss_kb = self.peak_rss_kb.max(kb);
                            }
                        }
                    }
                }
            }
        }
    }
    
    fn estimate_heap_size(&mut self, data_structures: Vec<(&str, usize)>) {
        let mut total_estimated = 0;
        
        for (name, size_bytes) in data_structures {
            let size_kb = size_bytes / 1024;
            total_estimated += size_kb;
            println!("  Heap structure '{}': {} KB", name, size_kb);
        }
        
        self.heap_size_estimated_kb = total_estimated as u64;
    }
    
    fn estimate_thread_overhead(&mut self, num_threads: usize) {
        const THREAD_STACK_SIZE_KB: u64 = 2048;
        const THREAD_METADATA_KB: u64 = 8;
        
        self.thread_overhead_kb = (num_threads as u64) * (THREAD_STACK_SIZE_KB + THREAD_METADATA_KB);
    }
    
    fn print_summary(&self, test_name: &str) {
        println!("\nMEMORY ANALYSIS: {}", test_name);
        println!("  Current RSS: {} KB ({:.1} MB)", self.current_rss_kb, self.current_rss_kb as f64 / 1024.0);
        println!("  Peak RSS: {} KB ({:.1} MB)", self.peak_rss_kb, self.peak_rss_kb as f64 / 1024.0);
        println!("  Estimated heap: {} KB ({:.1} MB)", self.heap_size_estimated_kb, self.heap_size_estimated_kb as f64 / 1024.0);
        println!("  Thread overhead: {} KB ({:.1} MB)", self.thread_overhead_kb, self.thread_overhead_kb as f64 / 1024.0);
        
        let runtime_overhead = self.current_rss_kb.saturating_sub(self.heap_size_estimated_kb + self.thread_overhead_kb);
        println!("  Runtime overhead: {} KB ({:.1} MB)", runtime_overhead, runtime_overhead as f64 / 1024.0);
    }
    
    fn get_peak_mb(&self) -> f64 {
        self.peak_rss_kb as f64 / 1024.0
    }
}

#[derive(Debug)]
struct ConcurrencyMetrics {
    mutex_operations: AtomicUsize,
    mutex_lock_times: AtomicU64,
    channel_operations: AtomicUsize,
    channel_latencies: AtomicU64,
    produced: AtomicUsize,
    consumed: AtomicUsize,
    start_time: Instant,
    memory_stats: Mutex<MemoryStats>,
}

impl ConcurrencyMetrics {
    fn new() -> Self {
        let mut memory_stats = MemoryStats::new();
        memory_stats.measure_current();
        
        Self {
            mutex_operations: AtomicUsize::new(0),
            mutex_lock_times: AtomicU64::new(0),
            channel_operations: AtomicUsize::new(0),
            channel_latencies: AtomicU64::new(0),
            produced: AtomicUsize::new(0),
            consumed: AtomicUsize::new(0),
            start_time: Instant::now(),
            memory_stats: Mutex::new(memory_stats),
        }
    }
    
    fn record_mutex_operation(&self, duration: Duration) {
        self.mutex_operations.fetch_add(1, Ordering::Relaxed);
        self.mutex_lock_times.fetch_add(duration.as_nanos() as u64, Ordering::Relaxed);
    }
    
    fn record_channel_operation(&self, duration: Duration) {
        self.channel_operations.fetch_add(1, Ordering::Relaxed);
        self.channel_latencies.fetch_add(duration.as_nanos() as u64, Ordering::Relaxed);
    }
    
    fn increment_produced(&self) {
        self.produced.fetch_add(1, Ordering::Relaxed);
    }
    
    fn increment_consumed(&self) {
        self.consumed.fetch_add(1, Ordering::Relaxed);
    }
    
    fn update_memory(&self, data_structures: Vec<(&str, usize)>, num_threads: usize) {
        if let Ok(mut stats) = self.memory_stats.lock() {
            stats.measure_current();
            stats.estimate_heap_size(data_structures);
            stats.estimate_thread_overhead(num_threads);
        }
    }
    
    fn get_elapsed_seconds(&self) -> f64 {
        self.start_time.elapsed().as_secs_f64()
    }
    
    fn get_mutex_ops_per_sec(&self) -> f64 {
        let elapsed = self.get_elapsed_seconds();
        if elapsed > 0.0 {
            self.mutex_operations.load(Ordering::Relaxed) as f64 / elapsed
        } else {
            0.0
        }
    }
    
    fn get_avg_mutex_time_us(&self) -> f64 {
        let ops = self.mutex_operations.load(Ordering::Relaxed);
        if ops > 0 {
            self.mutex_lock_times.load(Ordering::Relaxed) as f64 / ops as f64 / 1000.0
        } else {
            0.0
        }
    }
    
    fn get_efficiency(&self) -> f64 {
        let prod = self.produced.load(Ordering::Relaxed);
        if prod > 0 {
            self.consumed.load(Ordering::Relaxed) as f64 / prod as f64 * 100.0
        } else {
            0.0
        }
    }
    
    fn get_produced(&self) -> usize {
        self.produced.load(Ordering::Relaxed)
    }
    
    fn get_consumed(&self) -> usize {
        self.consumed.load(Ordering::Relaxed)
    }
    
    fn print_results(&self, test_name: &str) {
        let elapsed = self.get_elapsed_seconds();
        let produced = self.get_produced();
        let consumed = self.get_consumed();
        
        println!("\n{}", "=".repeat(60));
        println!("RUST BENCHMARK RESULTS: {}", test_name);
        println!("{}", "=".repeat(60));
        
        println!("EXECUTION:");
        println!("  Total time: {:.3} s", elapsed);
        println!("  Produced: {} ({:.2}/s)", produced, produced as f64 / elapsed);
        println!("  Consumed: {} ({:.2}/s)", consumed, consumed as f64 / elapsed);
        println!("  Efficiency: {:.1}%", self.get_efficiency());
        
        let mutex_ops = self.mutex_operations.load(Ordering::Relaxed);
        if mutex_ops > 0 {
            println!("\nMUTEX PERFORMANCE:");
            println!("  Operations: {} ({:.2} ops/s)", mutex_ops, self.get_mutex_ops_per_sec());
            println!("  Avg lock time: {:.2} μs", self.get_avg_mutex_time_us());
        }
        
        let channel_ops = self.channel_operations.load(Ordering::Relaxed);
        if channel_ops > 0 {
            println!("\nCHANNEL PERFORMANCE:");
            println!("  Operations: {} ({:.2} ops/s)", channel_ops, channel_ops as f64 / elapsed);
            println!("  Avg latency: {:.2} μs", 
                     self.channel_latencies.load(Ordering::Relaxed) as f64 / channel_ops as f64 / 1000.0);
        }
        
        if let Ok(stats) = self.memory_stats.lock() {
            stats.print_summary(test_name);
        }
    }
    
    fn get_peak_memory_mb(&self) -> f64 {
        if let Ok(stats) = self.memory_stats.lock() {
            stats.get_peak_mb()
        } else {
            0.0
        }
    }
}

struct ThreadSafeQueue<T> {
    queue: Arc<Mutex<VecDeque<T>>>,
}

impl<T> ThreadSafeQueue<T> {
    fn new() -> Self {
        Self {
            queue: Arc::new(Mutex::new(VecDeque::new())),
        }
    }
    
    fn push(&self, item: T) {
        let mut queue = self.queue.lock().unwrap();
        queue.push_back(item);
    }
    
    fn try_pop(&self) -> Option<T> {
        if let Ok(mut queue) = self.queue.try_lock() {
            queue.pop_front()
        } else {
            None
        }
    }
    
    fn is_empty(&self) -> bool {
        self.queue.lock().unwrap().is_empty()
    }
    
    fn get_memory_usage(&self) -> usize {
        let queue_guard = self.queue.lock().unwrap();
        std::mem::size_of::<VecDeque<T>>() + queue_guard.capacity() * std::mem::size_of::<T>()
    }
}

impl<T> Clone for ThreadSafeQueue<T> {
    fn clone(&self) -> Self {
        Self {
            queue: Arc::clone(&self.queue),
        }
    }
}

#[derive(Debug, Copy, Clone)]
enum ProducerConsumerMode {
    Channel,
    Queue,
}

fn producer_consumer_benchmark(mode: ProducerConsumerMode, num_producers: usize, num_consumers: usize, items_per_producer: usize) {
    match mode {
        ProducerConsumerMode::Channel => producer_consumer_channel_benchmark(num_producers, num_consumers, items_per_producer),
        ProducerConsumerMode::Queue => producer_consumer_queue_benchmark(num_producers, num_consumers, items_per_producer),
    }
}

fn producer_consumer_channel_benchmark(num_producers: usize, num_consumers: usize, items_per_producer: usize) {
    println!("\nPRODUCER-CONSUMER CHANNEL BENCHMARK (RUST)");
    println!("Producers: {}, Consumers: {}, Items per producer: {}", 
             num_producers, num_consumers, items_per_producer);
    
    let metrics = Arc::new(ConcurrencyMetrics::new());
    let (tx, rx) = mpsc::channel();
    let rx = Arc::new(Mutex::new(rx));
    let producers_done = Arc::new(AtomicBool::new(false));
    
    let total_threads = num_producers + num_consumers;
    let total_expected_items = num_producers * items_per_producer;
    
    let data_structures = vec![
        ("mpsc::channel", std::mem::size_of::<mpsc::Receiver<String>>() + std::mem::size_of::<mpsc::Sender<String>>()),
        ("Arc<Mutex<Receiver>>", std::mem::size_of::<Arc<Mutex<mpsc::Receiver<String>>>>()),
        ("Arc<AtomicBool>", std::mem::size_of::<Arc<AtomicBool>>()),
        ("String buffers (estimated)", total_expected_items * 32),
    ];
    
    metrics.update_memory(data_structures, total_threads);
    
    let mut producer_handles = Vec::new();
    let mut consumer_handles = Vec::new();
    
    for i in 0..num_producers {
        let tx = tx.clone();
        let metrics_clone = Arc::clone(&metrics);
        
        let handle = thread::spawn(move || {
            for j in 0..items_per_producer {
                let start_send = Instant::now();
                if tx.send(format!("Producer-{}-Item-{}", i, j)).is_ok() {
                    metrics_clone.increment_produced();
                    metrics_clone.record_channel_operation(start_send.elapsed());
                }
                
                if j % 100 == 0 {
                    thread::sleep(Duration::from_micros(1));
                }
            }
            println!("Rust Producer {} finished", i);
        });
        producer_handles.push(handle);
    }
    
    for i in 0..num_consumers {
        let rx = Arc::clone(&rx);
        let metrics_clone = Arc::clone(&metrics);
        let producers_done_clone = Arc::clone(&producers_done);
        
        let handle = thread::spawn(move || {
            let mut local_consumed = 0;
            loop {
                let start_recv = Instant::now();
                let received = {
                    if let Ok(rx_guard) = rx.try_lock() {
                        match rx_guard.try_recv() {
                            Ok(_item) => {
                                metrics_clone.record_channel_operation(start_recv.elapsed());
                                metrics_clone.increment_consumed();
                                local_consumed += 1;
                                true
                            }
                            Err(mpsc::TryRecvError::Empty) => {
                                if producers_done_clone.load(Ordering::Relaxed) {
                                    break;
                                }
                                false
                            }
                            Err(mpsc::TryRecvError::Disconnected) => break,
                        }
                    } else {
                        false
                    }
                };
                
                if !received {
                    thread::sleep(Duration::from_micros(10));
                }
            }
            println!("Rust Consumer {} finished, consumed {} items", i, local_consumed);
        });
        consumer_handles.push(handle);
    }
    
    drop(tx);
    
    for handle in producer_handles {
        handle.join().unwrap();
    }
    
    producers_done.store(true, Ordering::Relaxed);
    thread::sleep(Duration::from_millis(10));
    
    for handle in consumer_handles {
        handle.join().unwrap();
    }
    
    let final_data_structures = vec![
        ("Final channel state", 0),
        ("Cleanup overhead", 1024),
    ];
    metrics.update_memory(final_data_structures, 1);
    
    metrics.print_results("Producer-Consumer Channel");
}

fn producer_consumer_queue_benchmark(num_producers: usize, num_consumers: usize, items_per_producer: usize) {
    println!("\nPRODUCER-CONSUMER QUEUE BENCHMARK (RUST)");
    println!("Producers: {}, Consumers: {}, Items per producer: {}", 
             num_producers, num_consumers, items_per_producer);
    
    let metrics = Arc::new(ConcurrencyMetrics::new());
    let queue = ThreadSafeQueue::new();
    let producers_done = Arc::new(AtomicBool::new(false));
    
    let total_threads = num_producers + num_consumers;
    let total_expected_items = num_producers * items_per_producer;
    
    let data_structures = vec![
        ("ThreadSafeQueue<String>", queue.get_memory_usage()),
        ("Arc<AtomicBool>", std::mem::size_of::<Arc<AtomicBool>>()),
        ("String buffers (estimated)", total_expected_items * 32),
    ];
    
    metrics.update_memory(data_structures, total_threads);
    
    let mut producer_handles = Vec::new();
    let mut consumer_handles = Vec::new();
    
    for i in 0..num_producers {
        let queue = queue.clone();
        let metrics_clone = Arc::clone(&metrics);
        
        let handle = thread::spawn(move || {
            for j in 0..items_per_producer {
                let start_send = Instant::now();
                let item = format!("Producer-{}-Item-{}", i, j);
                queue.push(item);
                metrics_clone.increment_produced();
                metrics_clone.record_channel_operation(start_send.elapsed());
                
                if j % 100 == 0 {
                    thread::sleep(Duration::from_micros(1));
                }
            }
            println!("Rust Producer {} finished", i);
        });
        producer_handles.push(handle);
    }
    
    for i in 0..num_consumers {
        let queue = queue.clone();
        let metrics_clone = Arc::clone(&metrics);
        let producers_done_clone = Arc::clone(&producers_done);
        
        let handle = thread::spawn(move || {
            let mut local_consumed = 0;
            loop {
                let start_recv = Instant::now();
                
                if let Some(_item) = queue.try_pop() {
                    metrics_clone.record_channel_operation(start_recv.elapsed());
                    metrics_clone.increment_consumed();
                    local_consumed += 1;
                } else {
                    if producers_done_clone.load(Ordering::Relaxed) && queue.is_empty() {
                        break;
                    }
                    thread::sleep(Duration::from_micros(10));
                }
            }
            println!("Rust Consumer {} finished, consumed {} items", i, local_consumed);
        });
        consumer_handles.push(handle);
    }
    
    for handle in producer_handles {
        handle.join().unwrap();
    }
    
    producers_done.store(true, Ordering::Relaxed);
    thread::sleep(Duration::from_millis(10));
    
    for handle in consumer_handles {
        handle.join().unwrap();
    }
    
    let final_data_structures = vec![
        ("Final queue state", queue.get_memory_usage()),
        ("Cleanup overhead", 1024),
    ];
    metrics.update_memory(final_data_structures, 1);
    
    metrics.print_results("Producer-Consumer Queue");
}

fn shared_data_mutex_benchmark(num_threads: usize, operations_per_thread: usize) {
    println!("\nSHARED DATA MUTEX BENCHMARK (RUST)");
    println!("Threads: {}, Operations per thread: {}", num_threads, operations_per_thread);
    
    let metrics = Arc::new(ConcurrencyMetrics::new());
    let shared_counter = Arc::new(Mutex::new(0i64));
    let shared_vec = Arc::new(Mutex::new(Vec::<i32>::with_capacity(num_threads * operations_per_thread)));
    
    let total_expected_items = num_threads * operations_per_thread;
    let data_structures = vec![
        ("Arc<Mutex<i64>>", std::mem::size_of::<Arc<Mutex<i64>>>()),
        ("Arc<Mutex<Vec<i32>>>", std::mem::size_of::<Arc<Mutex<Vec<i32>>>>()),
        ("Vec<i32> capacity", total_expected_items * std::mem::size_of::<i32>()),
        ("Thread locals", num_threads * 256),
    ];
    
    metrics.update_memory(data_structures, num_threads);
    
    let handles: Vec<_> = (0..num_threads).map(|i| {
        let counter = Arc::clone(&shared_counter);
        let vec = Arc::clone(&shared_vec);
        let metrics_clone = Arc::clone(&metrics);
        
        thread::spawn(move || {
            for j in 0..operations_per_thread {
                {
                    let start_lock = Instant::now();
                    let mut num = counter.lock().unwrap();
                    *num += 1;
                    metrics_clone.record_mutex_operation(start_lock.elapsed());
                    metrics_clone.increment_produced();
                }
                
                {
                    let start_lock = Instant::now();
                    let mut v = vec.lock().unwrap();
                    let mut hasher = DefaultHasher::new();
                    (i * 1000 + j).hash(&mut hasher);
                    v.push(hasher.finish() as i32);
                    metrics_clone.record_mutex_operation(start_lock.elapsed());
                    metrics_clone.increment_consumed();
                }
                
                if j % 50 == 0 {
                    thread::sleep(Duration::from_micros(1));
                }
            }
            println!("Rust Mutex thread {} finished", i);
        })
    }).collect();
    
    for handle in handles {
        handle.join().unwrap();
    }
    
    let final_counter = *shared_counter.lock().unwrap();
    let final_vec_size = shared_vec.lock().unwrap().len();
    let final_vec_capacity = shared_vec.lock().unwrap().capacity();
    
    let final_data_structures = vec![
        ("Final Vec<i32> actual", final_vec_size * std::mem::size_of::<i32>()),
        ("Final Vec<i32> capacity", final_vec_capacity * std::mem::size_of::<i32>()),
        ("Cleanup overhead", 2048),
    ];
    metrics.update_memory(final_data_structures, 1);
    
    println!("\nMUTEX BENCHMARK RESULTS:");
    println!("  Final counter value: {}", final_counter);
    println!("  Final vector size: {} (capacity: {})", final_vec_size, final_vec_capacity);
    
    metrics.print_results("Shared Data Mutex");
}

fn benchmark_csv_output(max_threads: usize, items_per_test: usize) {
    println!("\nCSV OUTPUT FOR ANALYSIS:");
    println!("Threads,Execution_Time_Sec,Mutex_Ops_Per_Sec,Avg_Mutex_Time_Us,Peak_Memory_MB,RSS_Memory_MB,Efficiency_Percent");
    
    for threads in 1..=max_threads {
        let metrics = Arc::new(ConcurrencyMetrics::new());
        let shared_counter = Arc::new(Mutex::new(0i64));
        let shared_vec = Arc::new(Mutex::new(Vec::<i32>::with_capacity(threads * items_per_test)));
        
        let data_structures = vec![
            ("test_vectors", threads * items_per_test * std::mem::size_of::<i32>()),
            ("thread_overhead", threads * 2048),
            ("mutex_overhead", 512),
        ];
        metrics.update_memory(data_structures, threads);
        
        let handles: Vec<_> = (0..threads).map(|i| {
            let counter = Arc::clone(&shared_counter);
            let vec = Arc::clone(&shared_vec);
            let metrics_clone = Arc::clone(&metrics);
            
            thread::spawn(move || {
                for j in 0..items_per_test {
                    {
                        let start_lock = Instant::now();
                        let mut num = counter.lock().unwrap();
                        *num += 1;
                        metrics_clone.record_mutex_operation(start_lock.elapsed());
                        metrics_clone.increment_produced();
                    }
                    
                    {
                        let start_lock = Instant::now();
                        let mut v = vec.lock().unwrap();
                        let mut hasher = DefaultHasher::new();
                        (i * 1000 + j).hash(&mut hasher);
                        v.push(hasher.finish() as i32);
                        metrics_clone.record_mutex_operation(start_lock.elapsed());
                        metrics_clone.increment_consumed();
                    }
                    
                    if j % 50 == 0 {
                        thread::sleep(Duration::from_micros(1));
                    }
                }
            })
        }).collect();
        
        for handle in handles {
            handle.join().unwrap();
        }
        
        let final_data_structures = vec![
            ("final_state", shared_vec.lock().unwrap().len() * std::mem::size_of::<i32>()),
        ];
        metrics.update_memory(final_data_structures, 1);
        
        let peak_memory_mb = metrics.get_peak_memory_mb();
        
        if let Ok(stats) = metrics.memory_stats.lock() {
            println!("{},{:.3},{:.2},{:.2},{:.1},{:.1},{:.1}",
                     threads,
                     metrics.get_elapsed_seconds(),
                     metrics.get_mutex_ops_per_sec(),
                     metrics.get_avg_mutex_time_us(),
                     peak_memory_mb,
                     stats.current_rss_kb as f64 / 1024.0,
                     100.0);
        }
        
        thread::sleep(Duration::from_millis(100));
    }
}

#[derive(Debug)]
struct BenchmarkConfig {
    max_threads: usize,
    items_per_test: usize,
    csv_threads: usize,
    csv_items: usize,
    run_producer_consumer: bool,
    run_mutex_benchmark: bool,
    run_csv_output: bool,
    run_producer_consumer_ratio_test: bool,
    producer_consumer_mode: ProducerConsumerMode,
    help: bool,
}

impl Default for BenchmarkConfig {
    fn default() -> Self {
        Self {
            max_threads: std::thread::available_parallelism().map(|n| n.get()).unwrap_or(4),
            items_per_test: 10000,
            csv_threads: 8,
            csv_items: 1000,
            run_producer_consumer: true,
            run_mutex_benchmark: true,
            run_csv_output: true,
            run_producer_consumer_ratio_test: false,
            producer_consumer_mode: ProducerConsumerMode::Channel,
            help: false,
        }
    }
}

fn parse_args() -> BenchmarkConfig {
    let args: Vec<String> = env::args().collect();
    let mut config = BenchmarkConfig::default();
    let mut i = 1;
    
    while i < args.len() {
        match args[i].as_str() {
            "--help" | "-h" => {
                config.help = true;
                return config;
            }
            "--threads" | "-t" => {
                if i + 1 < args.len() {
                    match args[i + 1].parse::<usize>() {
                        Ok(n) if n > 0 && n <= 64 => config.max_threads = n,
                        Ok(n) => {
                            eprintln!("Warning: Thread count {} out of range (1-64), using default", n);
                        }
                        Err(_) => {
                            eprintln!("Warning: Invalid thread count '{}', using default", args[i + 1]);
                        }
                    }
                    i += 1;
                }
            }
            "--items" | "-i" => {
                if i + 1 < args.len() {
                    match args[i + 1].parse::<usize>() {
                        Ok(n) if n > 0 && n <= 1_000_000 => config.items_per_test = n,
                        Ok(n) => {
                            eprintln!("Warning: Items count {} out of range (1-1000000), using default", n);
                        }
                        Err(_) => {
                            eprintln!("Warning: Invalid items count '{}', using default", args[i + 1]);
                        }
                    }
                    i += 1;
                }
            }
            "--csv-threads" => {
                if i + 1 < args.len() {
                    match args[i + 1].parse::<usize>() {
                        Ok(n) if n > 0 && n <= 32 => config.csv_threads = n,
                        Ok(n) => {
                            eprintln!("Warning: CSV threads {} out of range (1-32), using default", n);
                        }
                        Err(_) => {
                            eprintln!("Warning: Invalid CSV threads '{}', using default", args[i + 1]);
                        }
                    }
                    i += 1;
                }
            }
            "--csv-items" => {
                if i + 1 < args.len() {
                    match args[i + 1].parse::<usize>() {
                        Ok(n) if n > 0 && n <= 100_000 => config.csv_items = n,
                        Ok(n) => {
                            eprintln!("Warning: CSV items {} out of range (1-100000), using default", n);
                        }
                        Err(_) => {
                            eprintln!("Warning: Invalid CSV items '{}', using default", args[i + 1]);
                        }
                    }
                    i += 1;
                }
            }
            "--no-producer-consumer" => {
                config.run_producer_consumer = false;
            }
            "--no-mutex" => {
                config.run_mutex_benchmark = false;
            }
            "--no-csv" => {
                config.run_csv_output = false;
            }
            "--mode" | "-m" => {
                if i + 1 < args.len() {
                    match args[i + 1].as_str() {
                        "channel" => config.producer_consumer_mode = ProducerConsumerMode::Channel,
                        "queue" => config.producer_consumer_mode = ProducerConsumerMode::Queue,
                        _ => {
                            eprintln!("Warning: Invalid mode '{}', using channel", args[i + 1]);
                        }
                    }
                    i += 1;
                }
            }
            "--ratio-test" => {
                config.run_producer_consumer_ratio_test = true;
            }
            _ => {
                if args[i].parse::<usize>().is_ok() && i == 1 {
                    if let Ok(n) = args[i].parse::<usize>() {
                        if n > 0 && n <= 64 {
                            config.max_threads = n;
                        }
                    }
                } else if args[i].parse::<usize>().is_ok() && i == 2 {
                    if let Ok(n) = args[i].parse::<usize>() {
                        if n > 0 && n <= 1_000_000 {
                            config.items_per_test = n;
                        }
                    }
                } else {
                    eprintln!("Warning: Unknown argument '{}'", args[i]);
                }
            }
        }
        i += 1;
    }
    
    config
}

fn print_usage() {
    let program_name = env::args().next().unwrap_or_else(|| "benchmark".to_string());
    
    println!("USAGE:");
    println!("  {} [OPTIONS] [max_threads] [items_per_test]", program_name);
    println!();
    println!("OPTIONS:");
    println!("  -h, --help                 Show this help message");
    println!("  -t, --threads <N>          Maximum number of threads (1-64, default: auto-detect)");
    println!("  -i, --items <N>            Number of items per test (1-1000000, default: 10000)");
    println!("  -m, --mode <MODE>          Producer-consumer mode: channel|queue (default: channel)");
    println!("  --csv-threads <N>          Threads for CSV output (1-32, default: 8)");
    println!("  --csv-items <N>            Items for CSV output (1-100000, default: 1000)");
    println!("  --no-producer-consumer     Skip producer-consumer benchmark");
    println!("  --no-mutex                 Skip mutex benchmark");
    println!("  --no-csv                   Skip CSV output");
    println!("  --ratio-test              Test different producer-consumer ratios");
    println!();
    println!("MEMORY ANALYSIS FEATURES:");
    println!("  - Real RSS memory measurement (Linux/macOS/Windows)");
    println!("  - Heap size estimation per data structure");
    println!("  - Thread overhead calculation");
    println!("  - Runtime overhead analysis");
    println!();
    println!("EXAMPLES:");
    println!("  {}                         # Default settings with memory analysis", program_name);
    println!("  {} --threads 4 --items 5000     # 4 threads, 5000 items", program_name);
    println!("  {} -t 8 -i 10000 -m queue       # 8 threads, queue mode", program_name);
    println!("  {} --no-csv                      # Skip CSV output", program_name);
}

fn get_os_info() -> &'static str {
    if cfg!(target_os = "windows") { "windows" }
    else if cfg!(target_os = "macos") { "macos" }
    else if cfg!(target_os = "linux") { "linux" }
    else { "unknown" }
}

fn get_cpu_architecture() -> &'static str {
    if cfg!(all(target_os = "macos", target_arch = "aarch64")) { "Apple Silicon" }
    else if cfg!(target_arch = "x86_64") { "x86_64" }
    else if cfg!(target_arch = "aarch64") { "aarch64" }
    else if cfg!(target_arch = "arm") { "arm" }
    else if cfg!(target_arch = "x86") { "x86 (32-bit)" }
    else { "unknown" }
}

fn producer_consumer_ratio_test(mode: ProducerConsumerMode, total_threads: usize, items_per_producer: usize) {
    println!("\nPRODUCER-CONSUMER RATIO TEST");
    println!("Testing different producer-consumer ratios with {:?} mode", mode);
    println!("Total threads: {}, Items per producer: {}", total_threads, items_per_producer);
    println!("\nProducers,Consumers,Total_Time_Sec,Messages_Per_Sec,Efficiency_Percent,Peak_Memory_MB");
    
    for producer_pct in [10, 20, 30, 40, 50, 60, 70, 80, 90] {
        let num_producers = (total_threads * producer_pct / 100).max(1);
        let num_consumers = (total_threads - num_producers).max(1);
        
        if num_producers == 0 || num_consumers == 0 {
            continue;
        }
        
        let metrics = Arc::new(ConcurrencyMetrics::new());
        
        match mode {
            ProducerConsumerMode::Channel => {
                let (tx, rx) = mpsc::channel();
                let rx = Arc::new(Mutex::new(rx));
                let producers_done = Arc::new(AtomicBool::new(false));
                
                let data_structures = vec![
                    ("ratio_test_channel", 1024),
                    ("estimated_messages", num_producers * items_per_producer * 32),
                ];
                metrics.update_memory(data_structures, num_producers + num_consumers);
                
                let mut producer_handles = Vec::new();
                let mut consumer_handles = Vec::new();
                
                for i in 0..num_producers {
                    let tx = tx.clone();
                    let metrics_clone = Arc::clone(&metrics);
                    
                    let handle = thread::spawn(move || {
                        for j in 0..items_per_producer {
                            let start_send = Instant::now();
                            if tx.send(format!("Producer-{}-Item-{}", i, j)).is_ok() {
                                metrics_clone.increment_produced();
                                metrics_clone.record_channel_operation(start_send.elapsed());
                            }
                            
                            if j % 100 == 0 {
                                thread::sleep(Duration::from_micros(1));
                            }
                        }
                    });
                    producer_handles.push(handle);
                }
                
                for _ in 0..num_consumers {
                    let rx = Arc::clone(&rx);
                    let metrics_clone = Arc::clone(&metrics);
                    let producers_done_clone = Arc::clone(&producers_done);
                    
                    let handle = thread::spawn(move || {
                        loop {
                            let start_recv = Instant::now();
                            let received = {
                                if let Ok(rx_guard) = rx.try_lock() {
                                    match rx_guard.try_recv() {
                                        Ok(_) => {
                                            metrics_clone.record_channel_operation(start_recv.elapsed());
                                            metrics_clone.increment_consumed();
                                            true
                                        }
                                        Err(mpsc::TryRecvError::Empty) => {
                                            if producers_done_clone.load(Ordering::Relaxed) {
                                                break;
                                            }
                                            false
                                        }
                                        Err(mpsc::TryRecvError::Disconnected) => break,
                                    }
                                } else {
                                    false
                                }
                            };
                            
                            if !received {
                                thread::sleep(Duration::from_micros(10));
                            }
                        }
                    });
                    consumer_handles.push(handle);
                }
                
                drop(tx);
                
                for handle in producer_handles {
                    handle.join().unwrap();
                }
                
                producers_done.store(true, Ordering::Relaxed);
                thread::sleep(Duration::from_millis(10));
                
                for handle in consumer_handles {
                    handle.join().unwrap();
                }
            },
            ProducerConsumerMode::Queue => {
                let queue = ThreadSafeQueue::new();
                let producers_done = Arc::new(AtomicBool::new(false));
                
                let data_structures = vec![
                    ("ratio_test_queue", queue.get_memory_usage()),
                    ("estimated_messages", num_producers * items_per_producer * 32),
                ];
                metrics.update_memory(data_structures, num_producers + num_consumers);
                
                let mut producer_handles = Vec::new();
                let mut consumer_handles = Vec::new();
                
                for i in 0..num_producers {
                    let queue = queue.clone();
                    let metrics_clone = Arc::clone(&metrics);
                    
                    let handle = thread::spawn(move || {
                        for j in 0..items_per_producer {
                            let start_send = Instant::now();
                            queue.push(format!("Producer-{}-Item-{}", i, j));
                            metrics_clone.increment_produced();
                            metrics_clone.record_channel_operation(start_send.elapsed());
                            
                            if j % 100 == 0 {
                                thread::sleep(Duration::from_micros(1));
                            }
                        }
                    });
                    producer_handles.push(handle);
                }
                
                for _ in 0..num_consumers {
                    let queue = queue.clone();
                    let metrics_clone = Arc::clone(&metrics);
                    let producers_done_clone = Arc::clone(&producers_done);
                    
                    let handle = thread::spawn(move || {
                        loop {
                            let start_recv = Instant::now();
                            
                            if let Some(_) = queue.try_pop() {
                                metrics_clone.record_channel_operation(start_recv.elapsed());
                                metrics_clone.increment_consumed();
                            } else {
                                if producers_done_clone.load(Ordering::Relaxed) && queue.is_empty() {
                                    break;
                                }
                                thread::sleep(Duration::from_micros(10));
                            }
                        }
                    });
                    consumer_handles.push(handle);
                }
                
                for handle in producer_handles {
                    handle.join().unwrap();
                }
                
                producers_done.store(true, Ordering::Relaxed);
                thread::sleep(Duration::from_millis(10));
                
                for handle in consumer_handles {
                    handle.join().unwrap();
                }
            }
        }
        
        let final_data_structures = vec![("ratio_test_cleanup", 512)];
        metrics.update_memory(final_data_structures, 1);
        
        let elapsed = metrics.get_elapsed_seconds();
        let consumed = metrics.get_consumed();
        let msgs_per_sec = consumed as f64 / elapsed;
        let efficiency = metrics.get_efficiency();
        let peak_memory_mb = metrics.get_peak_memory_mb();
        
        println!("{},{},{:.3},{:.2},{:.1},{:.1}", 
                 num_producers, 
                 num_consumers, 
                 elapsed, 
                 msgs_per_sec,
                 efficiency,
                 peak_memory_mb);
        
        thread::sleep(Duration::from_millis(100));
    }
}

fn main() {
    let config = parse_args();
    
    if config.help {
        print_usage();
        return;
    }
    
    let system_cores = std::thread::available_parallelism().map(|n| n.get()).unwrap_or(1);
    
    println!("{}", "=".repeat(80));
    println!("RUST CONCURRENCY MECHANISMS COMPREHENSIVE BENCHMARK");
    println!("WITH DETAILED MEMORY ANALYSIS");
    println!("{}", "=".repeat(80));
    
    println!("PLATFORM:");
    println!("  System: {}", get_os_info());
    println!("  Architecture: {}", get_cpu_architecture());
    println!("  Available cores: {}", system_cores);
    
    println!("\nMEMORY ANALYSIS CAPABILITIES:");
    if cfg!(target_os = "linux") {
        println!("  RSS measurement: /proc/self/status (Linux)");
    } else if cfg!(target_os = "macos") {
        println!("  RSS measurement: ps command (macOS)");
    } else if cfg!(target_os = "windows") {
        println!("  RSS measurement: tasklist command (Windows)");
    } else {
        println!("  RSS measurement: Not available on this platform");
    }
    println!("  Heap estimation: Per data structure analysis");
    println!("  Thread overhead: Stack + metadata calculation");
    println!("  Runtime overhead: Language runtime analysis");
    
    println!("\nCONFIGURATION:");
    println!("  Max threads used: {} {}", config.max_threads, 
             if config.max_threads > system_cores { "(exceeds physical cores)" } else { "" });
    println!("  Items per test: {}", config.items_per_test);
    println!("  Producer-consumer mode: {:?}", config.producer_consumer_mode);
    println!("  Concurrency scaling: {:.1}x logical cores", config.max_threads as f64 / system_cores as f64);
    println!("  Profile: {}", if cfg!(debug_assertions) { "debug" } else { "release" });
    
    if cfg!(debug_assertions) {
        println!("\nWARNING: Running in DEBUG mode! Use 'cargo run --release' for accurate benchmarks!");
    }
    
    if config.max_threads > system_cores * 2 {
        println!("\nWARNING: Using {}x more threads than cores may cause performance degradation", 
                 config.max_threads / system_cores);
    }
    
    let threads_per_test = config.max_threads.min(8);
    let producers_consumers = (threads_per_test / 2).max(1);
    
    println!("\nTEST SCENARIOS:");
    if config.run_producer_consumer {
        println!("  Producer-Consumer: {} producers, {} consumers", producers_consumers, producers_consumers);
    }
    if config.run_mutex_benchmark {
        println!("  Mutex contention: {} threads", threads_per_test);
    }
    if config.run_csv_output {
        println!("  CSV analysis: 1-{} threads, {} items each", config.csv_threads, config.csv_items);
    }
    if config.run_producer_consumer_ratio_test {
        println!("  Ratio analysis: Different producer/consumer ratios");
    }
    
    if config.run_producer_consumer {
        producer_consumer_benchmark(config.producer_consumer_mode, producers_consumers, producers_consumers, config.items_per_test);
    }
    
    if config.run_mutex_benchmark {
        shared_data_mutex_benchmark(threads_per_test, config.items_per_test);
    }
    
    if config.run_csv_output {
        benchmark_csv_output(config.csv_threads, config.csv_items);
    }
    
    if config.run_producer_consumer && config.run_producer_consumer_ratio_test {
        producer_consumer_ratio_test(
            config.producer_consumer_mode,
            config.max_threads.min(16),
            config.items_per_test / 2
        );
    }
    
    println!("\n{}", "=".repeat(80));
    println!("RUST BENCHMARK COMPLETED WITH MEMORY ANALYSIS");
    println!("{}", "=".repeat(80));
    println!("\nNOTE: Memory analysis includes:");
    println!("  - Real process memory (RSS) from OS");
    println!("  - Estimated heap usage per data structure");
    println!("  - Thread stack and metadata overhead");
    println!("  - Language runtime overhead calculation");
    println!("  - Peak vs current memory tracking");
}