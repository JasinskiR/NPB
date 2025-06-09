#!/usr/bin/env python3
"""
Comprehensive Benchmark Runner for Rust vs C++ Echo Server Performance
Supports multiple architectures (x86_64, ARM64) and operating systems (Linux, macOS, Windows)
"""

import subprocess
import time
import csv
import json
import os
import sys
import platform
import psutil
import argparse
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Tuple, Optional
import statistics
import threading
import signal
from dataclasses import dataclass, asdict

@dataclass
class SystemInfo:
    """System information for benchmark context"""
    architecture: str
    cpu_count: int
    cpu_freq_mhz: float
    memory_gb: float
    os_name: str
    os_version: str
    python_version: str

@dataclass
class BenchmarkConfig:
    """Configuration for a single benchmark run"""
    language: str
    implementation: str  # "threaded" or "async"
    num_clients: int
    messages_per_client: int
    max_connections: int
    message_size_kb: int
    num_threads: int

@dataclass
class BenchmarkResult:
    """Results from a single benchmark run"""
    config: BenchmarkConfig
    run_number: int
    timestamp: str
    success: bool
    
    # Timing metrics
    execution_time_seconds: Optional[float] = None
    
    # Server metrics
    total_connections: Optional[int] = None
    active_connections: Optional[int] = None
    peak_connections: Optional[int] = None
    connection_rate: Optional[float] = None
    avg_connection_duration_ms: Optional[float] = None
    
    # Throughput metrics
    total_messages: Optional[int] = None
    messages_per_second: Optional[float] = None
    total_bytes: Optional[int] = None
    throughput_mbps: Optional[float] = None
    
    # Efficiency metrics
    avg_bytes_per_message: Optional[float] = None
    messages_per_connection: Optional[float] = None
    
    # System metrics
    cpu_usage_percent: Optional[float] = None
    memory_usage_mb: Optional[float] = None
    
    # Language-specific metrics
    task_spawns: Optional[int] = None
    avg_spawn_time_us: Optional[float] = None
    tasks_per_second: Optional[float] = None
    async_operations: Optional[int] = None
    avg_operation_time_us: Optional[float] = None
    operations_per_second: Optional[float] = None
    
    # Error information
    error_message: Optional[str] = None
    stderr_output: Optional[str] = None

class BenchmarkRunner:
    def __init__(self, rust_binary: str = None, cpp_binary: str = None, 
                 output_dir: str = "benchmark_results", verbose: bool = False):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        self.verbose = verbose
        
        # Auto-detect binaries if not provided
        self.rust_binary = rust_binary or self._find_rust_binary()
        self.cpp_binary = cpp_binary or self._find_cpp_binary()
        
        self.system_info = self._get_system_info()
        self.results: List[BenchmarkResult] = []
        
        # Signal handling for graceful shutdown
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
        self._shutdown = False
        
    def _find_rust_binary(self) -> str:
        """Auto-detect Rust binary location"""
        possible_paths = [
            "./target/release/main",
            "./target/debug/main", 
            "./main",
            "cargo run --release --"
        ]
        
        for path in possible_paths:
            if "cargo run" in path:
                if subprocess.run(["cargo", "--version"], capture_output=True).returncode == 0:
                    return path
            elif Path(path).exists():
                return path
                
        raise FileNotFoundError("Rust binary not found. Please build the project or specify path.")
    
    def _find_cpp_binary(self) -> str:
        """Auto-detect C++ binary location"""
        possible_paths = [
            "./main",
            "./build/main",
            "./cmake-build-release/main",
            "./cmake-build-debug/main"
        ]
        
        for path in possible_paths:
            if Path(path).exists():
                return path
                
        raise FileNotFoundError("C++ binary not found. Please build the project or specify path.")
    
    def _get_system_info(self) -> SystemInfo:
        """Collect system information for benchmark context"""
        return SystemInfo(
            architecture=platform.machine(),
            cpu_count=psutil.cpu_count(logical=True),
            cpu_freq_mhz=psutil.cpu_freq().current if psutil.cpu_freq() else 0.0,
            memory_gb=psutil.virtual_memory().total / (1024**3),
            os_name=platform.system(),
            os_version=platform.release(),
            python_version=platform.python_version()
        )
    
    def _signal_handler(self, signum, frame):
        """Handle shutdown signals gracefully"""
        print(f"\nReceived signal {signum}. Shutting down gracefully...")
        self._shutdown = True
    
    def _parse_rust_output(self, output: str) -> Dict:
        """Parse Rust benchmark output to extract metrics"""
        metrics = {}
        lines = output.split('\n')
        
        for i, line in enumerate(lines):
            line = line.strip()
            
            # Parse execution time
            if "EXECUTION TIME:" in line:
                try:
                    metrics['execution_time_seconds'] = float(line.split(':')[1].strip().split()[0])
                except (IndexError, ValueError):
                    pass
            
            # Parse connection metrics
            elif "Total:" in line and "connections" not in line.lower():
                try:
                    metrics['total_connections'] = int(line.split(':')[1].strip())
                except (IndexError, ValueError):
                    pass
            elif "Active:" in line:
                try:
                    metrics['active_connections'] = int(line.split(':')[1].strip())
                except (IndexError, ValueError):
                    pass
            elif "Peak concurrent:" in line:
                try:
                    metrics['peak_connections'] = int(line.split(':')[1].strip())
                except (IndexError, ValueError):
                    pass
            elif "Rate:" in line and "conn/s" in line:
                try:
                    metrics['connection_rate'] = float(line.split(':')[1].strip().split()[0])
                except (IndexError, ValueError):
                    pass
            elif "Avg duration:" in line:
                try:
                    metrics['avg_connection_duration_ms'] = float(line.split(':')[1].strip().split()[0])
                except (IndexError, ValueError):
                    pass
            
            # Parse throughput metrics
            elif "Messages:" in line and "Messages/s" not in line:
                try:
                    metrics['total_messages'] = int(line.split(':')[1].strip())
                except (IndexError, ValueError):
                    pass
            elif "Messages/s:" in line:
                try:
                    metrics['messages_per_second'] = float(line.split(':')[1].strip())
                except (IndexError, ValueError):
                    pass
            elif "Bytes:" in line and "MB)" in line:
                try:
                    parts = line.split(':')[1].strip().split()
                    metrics['total_bytes'] = int(parts[0])
                except (IndexError, ValueError):
                    pass
            elif "Throughput:" in line and "MB/s" in line:
                try:
                    metrics['throughput_mbps'] = float(line.split(':')[1].strip().split()[0])
                except (IndexError, ValueError):
                    pass
            
            # Parse efficiency metrics
            elif "Avg bytes/message:" in line:
                try:
                    metrics['avg_bytes_per_message'] = float(line.split(':')[1].strip())
                except (IndexError, ValueError):
                    pass
            elif "Messages/connection:" in line:
                try:
                    metrics['messages_per_connection'] = float(line.split(':')[1].strip())
                except (IndexError, ValueError):
                    pass
            
            # Parse async-specific metrics
            elif "Total tasks spawned:" in line:
                try:
                    metrics['task_spawns'] = int(line.split(':')[1].strip())
                except (IndexError, ValueError):
                    pass
            elif "Avg spawn time:" in line:
                try:
                    metrics['avg_spawn_time_us'] = float(line.split(':')[1].strip().split()[0])
                except (IndexError, ValueError):
                    pass
            elif "Tasks per second:" in line:
                try:
                    metrics['tasks_per_second'] = float(line.split(':')[1].strip())
                except (IndexError, ValueError):
                    pass
            elif "Total operations:" in line:
                try:
                    metrics['async_operations'] = int(line.split(':')[1].strip())
                except (IndexError, ValueError):
                    pass
            elif "Avg operation time:" in line:
                try:
                    metrics['avg_operation_time_us'] = float(line.split(':')[1].strip().split()[0])
                except (IndexError, ValueError):
                    pass
            elif "Operations per second:" in line:
                try:
                    metrics['operations_per_second'] = float(line.split(':')[1].strip())
                except (IndexError, ValueError):
                    pass
        
        return metrics
    
    def _parse_cpp_output(self, output: str) -> Dict:
        """Parse C++ benchmark output to extract metrics"""
        # C++ output format is similar to Rust, so we can reuse the parser
        return self._parse_rust_output(output)
    
    def _run_single_benchmark(self, config: BenchmarkConfig, run_number: int) -> BenchmarkResult:
        """Run a single benchmark with given configuration"""
        timestamp = datetime.now().isoformat()
        
        if self._shutdown:
            return BenchmarkResult(config, run_number, timestamp, False, 
                                 error_message="Benchmark interrupted by user")
        
        # Build command based on language
        if config.language == "rust":
            if "cargo run" in self.rust_binary:
                cmd = self.rust_binary.split() + [
                    "--num-clients", str(config.num_clients),
                    "--messages-per-client", str(config.messages_per_client),
                    "--max-connections", str(config.max_connections),
                    "--message-size-kb", str(config.message_size_kb),
                    "--num-threads", str(config.num_threads)
                ]
            else:
                cmd = [
                    self.rust_binary,
                    "--num-clients", str(config.num_clients),
                    "--messages-per-client", str(config.messages_per_client),
                    "--max-connections", str(config.max_connections),
                    "--message-size-kb", str(config.message_size_kb),
                    "--num-threads", str(config.num_threads)
                ]
        else:  # C++
            cmd = [
                self.cpp_binary,
                "--num-clients", str(config.num_clients),
                "--messages-per-client", str(config.messages_per_client),
                "--max-connections", str(config.max_connections),
                "--message-size-kb", str(config.message_size_kb),
                "--num-threads", str(config.num_threads)
            ]
            
            if config.implementation == "async":
                cmd.append("--async")
        
        if self.verbose:
            print(f"Running: {' '.join(cmd)}")
            print(f"Working directory: {os.getcwd()}")
            print(f"Rust binary exists: {Path(self.rust_binary).exists() if not 'cargo' in self.rust_binary else 'Using cargo'}")
            print(f"C++ binary exists: {Path(self.cpp_binary).exists()}")
        
        # Monitor system resources
        process_monitor = threading.Thread(target=self._monitor_resources, 
                                         args=(config, run_number))
        
        start_time = time.time()
        
        try:
            # Start resource monitoring
            self._current_cpu_usage = []
            self._current_memory_usage = []
            self._monitoring = True
            process_monitor.start()
            
            # Run benchmark
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=300  # 5 minute timeout
            )
            
            execution_time = time.time() - start_time
            
            # Stop monitoring
            self._monitoring = False
            process_monitor.join(timeout=1)
            
            if result.returncode != 0:
                error_msg = f"Process failed with code {result.returncode}"
                if self.verbose:
                    print(f"\nSTDOUT:\n{result.stdout}")
                    print(f"\nSTDERR:\n{result.stderr}")
                return BenchmarkResult(
                    config, run_number, timestamp, False,
                    execution_time_seconds=execution_time,
                    error_message=error_msg,
                    stderr_output=result.stderr
                )
            
            # Parse output
            if config.language == "rust":
                metrics = self._parse_rust_output(result.stdout)
            else:
                metrics = self._parse_cpp_output(result.stdout)
            
            if self.verbose:
                print(f"\nParsed metrics: {metrics}")
                print(f"Output sample:\n{result.stdout[:500]}...")
            
            # Add system metrics
            if self._current_cpu_usage:
                metrics['cpu_usage_percent'] = statistics.mean(self._current_cpu_usage)
            if self._current_memory_usage:
                metrics['memory_usage_mb'] = statistics.mean(self._current_memory_usage)
            
            # Create result - remove execution_time from metrics if it exists to avoid conflict
            if 'execution_time_seconds' in metrics:
                del metrics['execution_time_seconds']
            
            benchmark_result = BenchmarkResult(
                config=config,
                run_number=run_number,
                timestamp=timestamp,
                success=True,
                execution_time_seconds=execution_time,
                **metrics
            )
            
            if self.verbose:
                print(f"✓ Completed {config.language}-{config.implementation} "
                      f"run {run_number} in {execution_time:.2f}s")
            
            return benchmark_result
            
        except subprocess.TimeoutExpired:
            self._monitoring = False
            return BenchmarkResult(
                config, run_number, timestamp, False,
                error_message="Benchmark timed out (>300s)"
            )
        except Exception as e:
            self._monitoring = False
            return BenchmarkResult(
                config, run_number, timestamp, False,
                error_message=f"Unexpected error: {str(e)}"
            )
    
    def _monitor_resources(self, config: BenchmarkConfig, run_number: int):
        """Monitor CPU and memory usage during benchmark"""
        while self._monitoring and not self._shutdown:
            try:
                self._current_cpu_usage.append(psutil.cpu_percent(interval=0.1))
                self._current_memory_usage.append(psutil.virtual_memory().used / (1024**2))
            except:
                break
            time.sleep(0.5)
    
    def run_benchmarks(self, configs: List[BenchmarkConfig], runs_per_config: int = 3) -> List[BenchmarkResult]:
        """Run all benchmark configurations multiple times"""
        total_runs = len(configs) * runs_per_config
        current_run = 0
        
        print(f"Starting benchmark suite: {len(configs)} configurations × {runs_per_config} runs = {total_runs} total runs")
        print(f"System: {self.system_info.os_name} {self.system_info.os_version} "
              f"on {self.system_info.architecture} ({self.system_info.cpu_count} cores)")
        
        for config in configs:
            if self._shutdown:
                break
                
            print(f"\n--- Testing {config.language}-{config.implementation} "
                  f"(clients:{config.num_clients}, msgs:{config.messages_per_client}, "
                  f"size:{config.message_size_kb}KB, threads:{config.num_threads}) ---")
            
            for run in range(runs_per_config):
                if self._shutdown:
                    break
                    
                current_run += 1
                print(f"[{current_run}/{total_runs}] Run {run + 1}/{runs_per_config}...", end=" ")
                
                result = self._run_single_benchmark(config, run + 1)
                self.results.append(result)
                
                if not result.success:
                    print(f"✗ FAILED: {result.error_message}")
                    if self.verbose and result.stderr_output:
                        print(f"  stderr: {result.stderr_output[:200]}...")
                else:
                    print(f"✓ {result.execution_time_seconds:.2f}s")
                
                # Small delay between runs
                time.sleep(1)
        
        return self.results
    
    def save_results(self, filename: str = None) -> str:
        """Save benchmark results to CSV file"""
        if not filename:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"benchmark_results_{timestamp}.csv"
        
        filepath = self.output_dir / filename
        
        if not self.results:
            print("No results to save.")
            return str(filepath)
        
        # Convert results to flat dictionaries for CSV
        fieldnames = set()
        flat_results = []
        
        for result in self.results:
            flat_result = {}
            
            # Add system info
            for key, value in asdict(self.system_info).items():
                flat_result[f"system_{key}"] = value
            
            # Add config info
            for key, value in asdict(result.config).items():
                flat_result[f"config_{key}"] = value
            
            # Add result info
            result_dict = asdict(result)
            del result_dict['config']  # Already added above
            
            for key, value in result_dict.items():
                flat_result[key] = value
            
            flat_results.append(flat_result)
            fieldnames.update(flat_result.keys())
        
        # Write CSV
        with open(filepath, 'w', newline='', encoding='utf-8') as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=sorted(fieldnames))
            writer.writeheader()
            writer.writerows(flat_results)
        
        print(f"Results saved to: {filepath}")
        return str(filepath)
    
    def generate_summary(self) -> Dict:
        """Generate summary statistics from results"""
        if not self.results:
            return {}
        
        summary = {
            'total_runs': len(self.results),
            'successful_runs': sum(1 for r in self.results if r.success),
            'failed_runs': sum(1 for r in self.results if not r.success),
            'configurations_tested': len(set((r.config.language, r.config.implementation, 
                                            r.config.num_clients, r.config.messages_per_client,
                                            r.config.message_size_kb, r.config.num_threads) 
                                           for r in self.results)),
            'languages_tested': list(set(r.config.language for r in self.results)),
            'system_info': asdict(self.system_info)
        }
        
        # Performance summary by language/implementation
        successful_results = [r for r in self.results if r.success and r.execution_time_seconds]
        
        if successful_results:
            by_impl = {}
            for result in successful_results:
                key = f"{result.config.language}_{result.config.implementation}"
                if key not in by_impl:
                    by_impl[key] = []
                by_impl[key].append(result)
            
            summary['performance_by_implementation'] = {}
            for impl, results in by_impl.items():
                exec_times = [r.execution_time_seconds for r in results if r.execution_time_seconds]
                throughputs = [r.throughput_mbps for r in results if r.throughput_mbps]
                
                summary['performance_by_implementation'][impl] = {
                    'runs': len(results),
                    'avg_execution_time': statistics.mean(exec_times) if exec_times else None,
                    'min_execution_time': min(exec_times) if exec_times else None,
                    'max_execution_time': max(exec_times) if exec_times else None,
                    'avg_throughput_mbps': statistics.mean(throughputs) if throughputs else None,
                    'max_throughput_mbps': max(throughputs) if throughputs else None
                }
        
        return summary

def create_benchmark_configurations() -> List[BenchmarkConfig]:
    """Create comprehensive set of benchmark configurations"""
    configs = []
    
    # Test scenarios with different load patterns
    scenarios = [
        # Light load
        {"clients": 10, "messages": 50, "connections": 100, "message_kb": 0},
        {"clients": 25, "messages": 100, "connections": 200, "message_kb": 1},
        
        # Medium load  
        {"clients": 50, "messages": 100, "connections": 500, "message_kb": 0},
        {"clients": 100, "messages": 50, "connections": 500, "message_kb": 4},
        
        # High load
        {"clients": 200, "messages": 25, "connections": 1000, "message_kb": 0},
        {"clients": 150, "messages": 100, "connections": 1000, "message_kb": 8},
        
        # Stress test
        {"clients": 500, "messages": 10, "connections": 2000, "message_kb": 0},
        {"clients": 100, "messages": 200, "connections": 1000, "message_kb": 16}
    ]
    
    # Thread counts to test
    thread_counts = [1, 2, 4, 8, psutil.cpu_count(logical=True)]
    thread_counts = sorted(list(set(thread_counts)))  # Remove duplicates and sort
    
    # Languages and implementations
    implementations = [
        ("rust", "async"),
        ("cpp", "threaded"),
        ("cpp", "async")
    ]
    
    for scenario in scenarios:
        for threads in thread_counts:
            for language, implementation in implementations:
                configs.append(BenchmarkConfig(
                    language=language,
                    implementation=implementation,
                    num_clients=scenario["clients"],
                    messages_per_client=scenario["messages"],
                    max_connections=scenario["connections"],
                    message_size_kb=scenario["message_kb"],
                    num_threads=threads
                ))
    
    return configs

def main():
    parser = argparse.ArgumentParser(description="Benchmark Rust vs C++ Echo Server Performance")
    parser.add_argument("--rust-binary", help="Path to Rust binary")
    parser.add_argument("--cpp-binary", help="Path to C++ binary")
    parser.add_argument("--output-dir", default="benchmark_results", help="Output directory")
    parser.add_argument("--runs", type=int, default=3, help="Runs per configuration")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    parser.add_argument("--quick", action="store_true", help="Quick test with fewer configurations")
    parser.add_argument("--config-file", help="JSON file with custom configurations")
    
    args = parser.parse_args()
    
    try:
        # Initialize benchmark runner
        runner = BenchmarkRunner(
            rust_binary=args.rust_binary,
            cpp_binary=args.cpp_binary,
            output_dir=args.output_dir,
            verbose=args.verbose
        )
        
        # Load configurations
        if args.config_file:
            with open(args.config_file, 'r') as f:
                config_data = json.load(f)
                configs = [BenchmarkConfig(**cfg) for cfg in config_data]
        elif args.quick:
            # Quick test configurations
            configs = [
                BenchmarkConfig("rust", "async", 10, 20, 100, 0, 2),
                BenchmarkConfig("cpp", "threaded", 10, 20, 100, 0, 2),
                BenchmarkConfig("cpp", "async", 10, 20, 100, 0, 2),
            ]
        else:
            configs = create_benchmark_configurations()
        
        print(f"Loaded {len(configs)} benchmark configurations")
        
        # Run benchmarks
        results = runner.run_benchmarks(configs, args.runs)
        
        # Save results
        csv_file = runner.save_results()
        
        # Generate and save summary
        summary = runner.generate_summary()
        summary_file = runner.output_dir / "benchmark_summary.json"
        with open(summary_file, 'w') as f:
            json.dump(summary, f, indent=2, default=str)
        
        print(f"\nBenchmark completed!")
        print(f"Total runs: {summary['total_runs']}")
        print(f"Successful: {summary['successful_runs']}")
        print(f"Failed: {summary['failed_runs']}")
        print(f"Results: {csv_file}")
        print(f"Summary: {summary_file}")
        
        if summary.get('performance_by_implementation'):
            print("\nPerformance Summary:")
            for impl, stats in summary['performance_by_implementation'].items():
                print(f"  {impl}: avg_time={stats.get('avg_execution_time', 'N/A'):.2f}s, "
                      f"max_throughput={stats.get('max_throughput_mbps', 'N/A'):.2f} MB/s")
        
    except KeyboardInterrupt:
        print("\nBenchmark interrupted by user.")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()