use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::net::SocketAddr;
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, AtomicU64, Ordering};
use std::time::{Duration, Instant};
use tokio::sync::{Semaphore, broadcast, RwLock};
use tokio::time::{sleep, timeout};
use sysinfo::{System, SystemExt, ProcessExt, Pid};
use clap::Parser;
use rand::{thread_rng, Rng};
use rand::distributions::{Alphanumeric, DistString};

// Command line argument parsing
#[derive(Parser, Debug)]
#[command(author, version, about = "Tokio async benchmarking tool")]
struct Args {
    /// Number of echo clients to spawn
    #[arg(long, default_value_t = 50)]
    num_clients: usize,
    
    /// Number of messages per client
    #[arg(long, default_value_t = 100)]
    messages_per_client: usize,
    
    /// Maximum number of concurrent connections allowed
    #[arg(long, default_value_t = 1000)]
    max_connections: usize,
    
    /// Size of message payload in KB (0 for default small messages)
    #[arg(long, default_value_t = 0)]
    message_size_kb: usize,
    
    /// Number of Tokio worker threads (0 = default based on CPU cores)
    #[arg(long, default_value_t = 0)]
    num_threads: usize,
}

#[derive(Debug)]
struct AsyncMetrics {
    task_spawns: AtomicUsize,
    task_spawn_times: AtomicU64, // nanoseconds
    async_operations: AtomicUsize,
    async_operation_times: AtomicU64, // nanoseconds
    memory_snapshots: RwLock<Vec<u64>>, // KB
    start_time: Instant,
}

impl AsyncMetrics {
    fn new() -> Self {
        Self {
            task_spawns: AtomicUsize::new(0),
            task_spawn_times: AtomicU64::new(0),
            async_operations: AtomicUsize::new(0),
            async_operation_times: AtomicU64::new(0),
            memory_snapshots: RwLock::new(Vec::new()),
            start_time: Instant::now(),
        }
    }
    
    fn record_task_spawn(&self, duration: Duration) {
        self.task_spawns.fetch_add(1, Ordering::Relaxed);
        self.task_spawn_times.fetch_add(duration.as_nanos() as u64, Ordering::Relaxed);
    }
    
    fn record_async_operation(&self, duration: Duration) {
        self.async_operations.fetch_add(1, Ordering::Relaxed);
        self.async_operation_times.fetch_add(duration.as_nanos() as u64, Ordering::Relaxed);
    }
    
    async fn take_memory_snapshot(&self) {
        if let Some(memory_kb) = get_current_memory_usage() {
            let mut snapshots = self.memory_snapshots.write().await;
            snapshots.push(memory_kb);
        }
    }
    
    async fn print_metrics(&self, test_name: &str) {
        let elapsed = self.start_time.elapsed();
        let task_spawns = self.task_spawns.load(Ordering::Relaxed);
        let task_spawn_times = self.task_spawn_times.load(Ordering::Relaxed);
        let async_ops = self.async_operations.load(Ordering::Relaxed);
        let async_op_times = self.async_operation_times.load(Ordering::Relaxed);
        
        println!("\n{:=<60}", "");
        println!("ASYNC METRICS: {}", test_name);
        println!("{:=<60}", "");
        
        println!("EXECUTION TIME: {:.3} seconds", elapsed.as_secs_f64());
        
        if task_spawns > 0 {
            println!("\nTASK SPAWNING:");
            println!("  Total tasks spawned: {}", task_spawns);
            println!("  Avg spawn time: {:.2} μs", 
                     task_spawn_times as f64 / task_spawns as f64 / 1000.0);
            println!("  Tasks per second: {:.2}", task_spawns as f64 / elapsed.as_secs_f64());
        }
        
        if async_ops > 0 {
            println!("\nASYNC OPERATIONS:");
            println!("  Total operations: {}", async_ops);
            println!("  Avg operation time: {:.2} μs", 
                     async_op_times as f64 / async_ops as f64 / 1000.0);
            println!("  Operations per second: {:.2}", async_ops as f64 / elapsed.as_secs_f64());
        }
        
        let snapshots = self.memory_snapshots.read().await;
        if !snapshots.is_empty() {
            let min_mem = *snapshots.iter().min().unwrap() as f64 / 1024.0;
            let max_mem = *snapshots.iter().max().unwrap() as f64 / 1024.0;
            let avg_mem = snapshots.iter().sum::<u64>() as f64 / snapshots.len() as f64 / 1024.0;
            
            println!("\nMEMORY USAGE:");
            println!("  Min: {:.2} MB", min_mem);
            println!("  Max: {:.2} MB", max_mem);
            println!("  Avg: {:.2} MB", avg_mem);
            println!("  Growth: {:.2} MB", max_mem - min_mem);
        }
    }
}

#[derive(Debug)]
struct EchoServerMetrics {
    connections_accepted: AtomicUsize,
    messages_echoed: AtomicUsize,
    bytes_transferred: AtomicU64,
    connection_durations: AtomicU64, // microseconds
    active_connections: AtomicUsize,
    peak_connections: AtomicUsize,
    start_time: Instant,
}

impl EchoServerMetrics {
    fn new() -> Self {
        Self {
            connections_accepted: AtomicUsize::new(0),
            messages_echoed: AtomicUsize::new(0),
            bytes_transferred: AtomicU64::new(0),
            connection_durations: AtomicU64::new(0),
            active_connections: AtomicUsize::new(0),
            peak_connections: AtomicUsize::new(0),
            start_time: Instant::now(),
        }
    }
    
    fn connection_started(&self) -> usize {
        let conn_id = self.connections_accepted.fetch_add(1, Ordering::Relaxed);
        let active = self.active_connections.fetch_add(1, Ordering::Relaxed) + 1;
        
        // Update peak connections
        let mut peak = self.peak_connections.load(Ordering::Relaxed);
        while active > peak {
            match self.peak_connections.compare_exchange_weak(
                peak, active, Ordering::Relaxed, Ordering::Relaxed
            ) {
                Ok(_) => break,
                Err(new_peak) => peak = new_peak,
            }
        }
        
        conn_id
    }
    
    fn connection_ended(&self, duration: Duration, messages: usize, bytes: u64) {
        self.active_connections.fetch_sub(1, Ordering::Relaxed);
        self.messages_echoed.fetch_add(messages, Ordering::Relaxed);
        self.bytes_transferred.fetch_add(bytes, Ordering::Relaxed);
        self.connection_durations.fetch_add(duration.as_micros() as u64, Ordering::Relaxed);
    }
    
    fn print_metrics(&self) {
        let elapsed = self.start_time.elapsed();
        let connections = self.connections_accepted.load(Ordering::Relaxed);
        let messages = self.messages_echoed.load(Ordering::Relaxed);
        let bytes = self.bytes_transferred.load(Ordering::Relaxed);
        let total_duration = self.connection_durations.load(Ordering::Relaxed);
        let peak_connections = self.peak_connections.load(Ordering::Relaxed);
        let active_connections = self.active_connections.load(Ordering::Relaxed);
        
        println!("\nECHO SERVER METRICS:");
        println!("============================================================");
        println!("DURATION: {:.3} seconds", elapsed.as_secs_f64());
        
        println!("\nCONNECTIONS:");
        println!("  Total: {}", connections);
        println!("  Active: {}", active_connections);
        println!("  Peak concurrent: {}", peak_connections);
        println!("  Rate: {:.2} conn/s", connections as f64 / elapsed.as_secs_f64());
        if connections > 0 {
            println!("  Avg duration: {:.2} ms", total_duration as f64 / connections as f64 / 1000.0);
        }
        
        println!("\nTHROUGHPUT:");
        println!("  Messages: {}", messages);
        println!("  Messages/s: {:.2}", messages as f64 / elapsed.as_secs_f64());
        println!("  Bytes: {} ({:.2} MB)", bytes, bytes as f64 / (1024.0 * 1024.0));
        println!("  Throughput: {:.2} MB/s", bytes as f64 / (1024.0 * 1024.0) / elapsed.as_secs_f64());
        
        if messages > 0 && connections > 0 {
            println!("\nEFFICIENCY:");
            println!("  Avg bytes/message: {:.1}", bytes as f64 / messages as f64);
            println!("  Messages/connection: {:.1}", messages as f64 / connections as f64);
        }
    }
}

fn get_current_memory_usage() -> Option<u64> {
    let mut system = System::new_all();
    system.refresh_processes();
    let pid = Pid::from(std::process::id() as usize);
    system.process(pid).map(|p| p.memory())
}

async fn handle_echo_client(
    mut stream: TcpStream, 
    addr: SocketAddr, 
    metrics: Arc<EchoServerMetrics>
) {
    let connection_start = Instant::now();
    let conn_id = metrics.connection_started();
    let mut buffer = [0; 1024];
    let mut total_bytes = 0u64;
    let mut message_count = 0;
    
    println!("Client connected: {} (ID: {})", addr, conn_id);
    
    loop {
        match timeout(Duration::from_secs(30), stream.read(&mut buffer)).await {
            Ok(Ok(0)) => break, // Connection closed
            Ok(Ok(n)) => {
                // Echo the data back
                if stream.write_all(&buffer[0..n]).await.is_err() {
                    break;
                }
                total_bytes += (n * 2) as u64; // Read + write
                message_count += 1;
            }
            Ok(Err(_)) | Err(_) => break, // Error or timeout
        }
    }
    
    let duration = connection_start.elapsed();
    metrics.connection_ended(duration, message_count, total_bytes);
    println!("Client disconnected: {} (ID: {}, {}ms, {} messages, {} bytes)", 
             addr, conn_id, duration.as_millis(), message_count, total_bytes);
}

async fn run_echo_server(
    addr: &str, 
    max_connections: usize,
    shutdown_rx: &mut broadcast::Receiver<()>
) -> Arc<EchoServerMetrics> {
    println!("Starting echo server on {}", addr);
    
    let listener = TcpListener::bind(addr).await.expect("Failed to bind");
    let metrics = Arc::new(EchoServerMetrics::new());
    let semaphore = Arc::new(Semaphore::new(max_connections));
    
    println!("Echo server listening on {} (max {} connections)", addr, max_connections);
    
    loop {
        tokio::select! {
            // Handle new connections
            accept_result = listener.accept() => {
                match accept_result {
                    Ok((stream, addr)) => {
                        if let Ok(permit) = semaphore.clone().try_acquire_owned() {
                            let metrics_clone = Arc::clone(&metrics);
                            tokio::spawn(async move {
                                handle_echo_client(stream, addr, metrics_clone).await;
                                drop(permit);
                            });
                        } else {
                            println!("Connection rejected: max capacity reached");
                            drop(stream);
                        }
                    }
                    Err(e) => {
                        eprintln!("Failed to accept connection: {}", e);
                    }
                }
            }
            
            // Handle shutdown signal
            _ = shutdown_rx.recv() => {
                println!("Echo server shutting down...");
                break;
            }
        }
    }
    
    metrics
}

async fn echo_client_benchmark(
    server_addr: &str, 
    num_clients: usize, 
    messages_per_client: usize,
    message_size_kb: usize,
    metrics: Arc<AsyncMetrics>
) {
    println!("\nECHO CLIENT BENCHMARK");
    println!("Clients: {}, Messages per client: {}, Message size: {} KB", 
             num_clients, messages_per_client, if message_size_kb > 0 { message_size_kb } else { 0 });
    
    let mut handles = vec![];
    
    for client_id in 0..num_clients {
        let addr = server_addr.to_string();
        let metrics_clone = Arc::clone(&metrics);
        
        let spawn_start = Instant::now();
        let handle = tokio::spawn(async move {
            metrics_clone.record_task_spawn(spawn_start.elapsed());
            
            if let Ok(mut stream) = TcpStream::connect(&addr).await {
                for msg_id in 0..messages_per_client {
                    let message: Vec<u8> = if message_size_kb > 0 {
                        // Generate random payload of specified size
                        let size = message_size_kb * 1024;
                        let mut rng = thread_rng();
                        let mut payload = Vec::with_capacity(size);
                        // Generate payload in chunks to avoid large allocations
                        for _ in 0..(size / 64) {
                            payload.extend_from_slice(Alphanumeric.sample_string(&mut rng, 64).as_bytes());
                        }
                        // Add remaining bytes
                        let remaining = size % 64;
                        if remaining > 0 {
                            payload.extend_from_slice(Alphanumeric.sample_string(&mut rng, remaining).as_bytes());
                        }
                        payload
                    } else {
                        // Use default small message
                        format!("Client-{}-Message-{}", client_id, msg_id).into_bytes()
                    };
                    
                    let op_start = Instant::now();
                    
                    // Send message
                    if stream.write_all(&message).await.is_err() {
                        break;
                    }
                    
                    // Read echo
                    let mut buffer = vec![0; message.len()];
                    if stream.read_exact(&mut buffer).await.is_err() {
                        break;
                    }
                    
                    metrics_clone.record_async_operation(op_start.elapsed());
                    
                    // Small delay to simulate realistic usage
                    sleep(Duration::from_millis(1)).await;
                }
                println!("Client {} finished", client_id);
            } else {
                eprintln!("Client {} failed to connect", client_id);
            }
        });
        metrics.record_task_spawn(spawn_start.elapsed());
        handles.push(handle);
    }
    
    for handle in handles {
        let _ = handle.await;
    }
}

async fn comprehensive_tokio_benchmark(args: Args, num_threads: usize) {
    println!("{:=<80}", "");
    println!("TOKIO ECHO SERVER BENCHMARK");
    println!("{:=<80}", "");
    
    let metrics = Arc::new(AsyncMetrics::new());
    
    // Get system info
    let cores = std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(4);
    
    println!("System cores: {}", cores);
    println!("Tokio worker threads: {}", num_threads);
    println!("Tokio version: {}", env!("CARGO_PKG_VERSION"));
    println!("Benchmark configuration:");
    println!("  - Clients: {}", args.num_clients);
    println!("  - Messages per client: {}", args.messages_per_client);
    println!("  - Max connections: {}", args.max_connections);
    println!("  - Message size: {} KB", if args.message_size_kb > 0 { args.message_size_kb } else { 0 });
    
    // Print initial memory
    if let Some(mem) = get_current_memory_usage() {
        println!("Initial memory: {:.2} MB", mem as f64 / 1024.0);
    }
    
    // Echo server benchmark
    let (shutdown_tx, mut shutdown_rx) = broadcast::channel(1);
    
    let server_addr = "127.0.0.1:9999";
    let server_metrics = Arc::new(AsyncMetrics::new());
    
    // Start server
    let server_handle = {
        let mut shutdown_rx = shutdown_tx.subscribe();
        let max_connections = args.max_connections;
        tokio::spawn(async move {
            run_echo_server(server_addr, max_connections, &mut shutdown_rx).await
        })
    };
    
    // Give server time to start
    sleep(Duration::from_millis(100)).await;
    
    // Run client benchmark
    echo_client_benchmark(
        server_addr, 
        args.num_clients, 
        args.messages_per_client, 
        args.message_size_kb,
        Arc::clone(&server_metrics)
    ).await;
    
    // Shutdown server
    let _ = shutdown_tx.send(());
    if let Ok(echo_metrics) = server_handle.await {
        echo_metrics.print_metrics();
    }
    
    server_metrics.print_metrics("Echo Server Client").await;
    
    // Print final memory
    if let Some(mem) = get_current_memory_usage() {
        println!("\nFinal memory: {:.2} MB", mem as f64 / 1024.0);
    }
    
    println!("\n{:=<80}", "");
    println!("ECHO SERVER BENCHMARK COMPLETED");
    println!("{:=<80}", "");
}

fn main() {
    let args = Args::parse();
    
    // Determine the number of worker threads
    let num_threads = if args.num_threads > 0 {
        args.num_threads
    } else {
        std::thread::available_parallelism()
            .map(|n| n.get())
            .unwrap_or(4)
    };
    
    // Build custom runtime with specified number of threads
    let rt = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(num_threads)
        .enable_all()
        .build()
        .expect("Failed to create Tokio runtime");
    
    // Run the async main function
    rt.block_on(comprehensive_tokio_benchmark(args, num_threads));
}