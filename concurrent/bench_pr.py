#!/usr/bin/env python3
"""
Comprehensive Benchmark Runner for Rust vs C++ Concurrency Performance
Runs multiple test scenarios and saves results to CSV files for analysis.
"""

import subprocess
import csv
import time
import os
import sys
import platform
import psutil
import json
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Tuple, Optional
import argparse
import re

class BenchmarkConfig:
    """Configuration for benchmark test cases"""
    
    def __init__(self):
        self.test_cases = [
            # Light load tests
            {"threads": 2, "items": 1000, "description": "Light load - 2 threads"},
            {"threads": 4, "items": 2000, "description": "Light load - 4 threads"},
            
            # Medium load tests
            {"threads": 4, "items": 10000, "description": "Medium load - 4 threads"},
            {"threads": 8, "items": 10000, "description": "Medium load - 8 threads"},
            {"threads": 8, "items": 20000, "description": "Medium load - 8 threads, more items"},
            
            # Heavy load tests
            {"threads": 16, "items": 10000, "description": "Heavy load - 16 threads"},
            {"threads": 16, "items": 25000, "description": "Heavy load - 16 threads, many items"},
            {"threads": 32, "items": 10000, "description": "Very heavy load - 32 threads"},
            
            # Extreme cases
            {"threads": 64, "items": 5000, "description": "Extreme threads"},
            {"threads": 8, "items": 100000, "description": "Extreme items"},
        ]
        
        self.modes = ["channel", "queue"]
        self.repeat_count = 3  # Number of times to repeat each test for averaging
        
    def get_stress_test_cases(self) -> List[Dict]:
        """Generate stress test cases that push the limits"""
        stress_cases = []
        
        # Thread scaling tests (fixed items, varying threads)
        for threads in [1, 2, 3, 4, 5, 6, 7, 8]:
            stress_cases.append({
                "threads": threads,
                "items": 10000,
                "description": f"Thread scaling - {threads} threads"
            })
        
        # Item scaling tests (fixed threads, varying items)
        for items in [1000, 5000, 10000, 25000, 50000]:
            stress_cases.append({
                "threads": 8,
                "items": items,
                "description": f"Item scaling - {items} items"
            })
        
        return stress_cases

class SystemInfo:
    """Collect system information for benchmark context"""
    
    @staticmethod
    def get_system_info() -> Dict:
        return {
            "platform": platform.platform(),
            "system": platform.system(),
            "machine": platform.machine(),
            "processor": platform.processor(),
            "cpu_count_logical": psutil.cpu_count(logical=True),
            "cpu_count_physical": psutil.cpu_count(logical=False),
            "memory_total_gb": round(psutil.virtual_memory().total / (1024**3), 2),
            "python_version": platform.python_version(),
            "timestamp": datetime.now().isoformat(),
        }

class BenchmarkResult:
    """Container for benchmark results"""
    
    def __init__(self):
        self.language = ""
        self.mode = ""
        self.threads = 0
        self.items = 0
        self.execution_time = 0.0
        self.produced = 0
        self.consumed = 0
        self.efficiency = 0.0
        self.mutex_ops_per_sec = 0.0
        self.avg_mutex_time_us = 0.0
        self.messages_per_sec = 0.0
        self.peak_memory_mb = 0.0
        self.description = ""
        self.error = None
        
    def to_dict(self) -> Dict:
        return {
            "language": self.language,
            "mode": self.mode,
            "threads": self.threads,
            "items": self.items,
            "execution_time_sec": self.execution_time,
            "produced": self.produced,
            "consumed": self.consumed,
            "efficiency_percent": self.efficiency,
            "mutex_ops_per_sec": self.mutex_ops_per_sec,
            "avg_mutex_time_us": self.avg_mutex_time_us,
            "messages_per_sec": self.messages_per_sec,
            "peak_memory_mb": self.peak_memory_mb,
            "description": self.description,
            "error": self.error
        }

class BenchmarkRunner:
    """Main benchmark runner class"""
    
    def __init__(self, rust_binary: str = "./main", cpp_binary: str = "./main_cpp"):
        self.rust_binary = rust_binary
        self.cpp_binary = cpp_binary
        self.results: List[BenchmarkResult] = []
        self.system_info = SystemInfo.get_system_info()
        
    def check_binaries(self) -> bool:
        """Check if benchmark binaries exist and are executable"""
        rust_exists = Path(self.rust_binary).is_file()
        cpp_exists = Path(self.cpp_binary).is_file()
        
        if not rust_exists:
            print(f"Rust binary not found: {self.rust_binary}")
            print("   Please compile with: cargo build --release")
            
        if not cpp_exists:
            print(f"C++ binary not found: {self.cpp_binary}")
            print("   Please compile with: g++ -O3 -std=c++20 -pthread main.cpp -o main_cpp")
            
        return rust_exists and cpp_exists
    
    def run_single_benchmark(self, binary: str, language: str, threads: int, 
                           items: int, mode: str, timeout: int = 300) -> BenchmarkResult:
        """Run a single benchmark and parse results"""
        result = BenchmarkResult()
        result.language = language
        result.mode = mode
        result.threads = threads
        result.items = items
        
        try:
            # Prepare command
            cmd = [binary, "--threads", str(threads), "--items", str(items), 
                   "--mode", mode, "--no-csv"]
            
            print(f"  Running: {' '.join(cmd)}")
            
            # Run benchmark
            start_time = time.time()
            process = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=timeout
            )
            actual_time = time.time() - start_time
            
            if process.returncode != 0:
                result.error = f"Process failed with return code {process.returncode}: {process.stderr}"
                return result
                
            # Parse output
            output = process.stdout
            result = self._parse_benchmark_output(output, result)
            
            # If parsing failed to get execution time, use measured time
            if result.execution_time == 0:
                result.execution_time = actual_time
                
        except subprocess.TimeoutExpired:
            result.error = f"Timeout after {timeout} seconds"
        except Exception as e:
            result.error = f"Exception: {str(e)}"
            
        return result
    
    def _parse_benchmark_output(self, output: str, result: BenchmarkResult) -> BenchmarkResult:
        """Parse benchmark output to extract metrics"""
        lines = output.split('\n')
        
        for line in lines:
            line = line.strip()
            
            # Parse execution metrics
            if "Total time:" in line:
                match = re.search(r"Total time:\s+([\d.]+)\s+s", line)
                if match:
                    result.execution_time = float(match.group(1))
                    
            elif "Produced:" in line:
                match = re.search(r"Produced:\s+(\d+)\s+\(([\d.]+)/s\)", line)
                if match:
                    result.produced = int(match.group(1))
                    
            elif "Consumed:" in line:
                match = re.search(r"Consumed:\s+(\d+)\s+\(([\d.]+)/s\)", line)
                if match:
                    result.consumed = int(match.group(1))
                    result.messages_per_sec = float(match.group(2))
                    
            elif "Efficiency:" in line:
                match = re.search(r"Efficiency:\s+([\d.]+)%", line)
                if match:
                    result.efficiency = float(match.group(1))
                    
            elif "Operations:" in line and "ops/s" in line:
                match = re.search(r"Operations:\s+\d+\s+\(([\d.]+)\s+ops/s\)", line)
                if match:
                    result.mutex_ops_per_sec = float(match.group(1))
                    
            elif "Avg lock time:" in line:
                match = re.search(r"Avg lock time:\s+([\d.]+)\s+μs", line)
                if match:
                    result.avg_mutex_time_us = float(match.group(1))
                    
            elif "Peak memory:" in line:
                match = re.search(r"Peak memory:\s+([\d.]+)\s+MB", line)
                if match:
                    result.peak_memory_mb = float(match.group(1))
        
        return result
    
    def run_ratio_test(self, binary: str, language: str, total_threads: int, 
                      items: int, mode: str) -> List[BenchmarkResult]:
        """Run producer-consumer ratio test"""
        results = []
        
        try:
            cmd = [binary, "--threads", str(total_threads), "--items", str(items),
                   "--mode", mode, "--ratio-test", "--no-producer-consumer", 
                   "--no-mutex", "--no-csv"]
            
            print(f"  Running ratio test: {' '.join(cmd)}")
            
            process = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
            
            if process.returncode != 0:
                print(f"    Ratio test failed: {process.stderr}")
                return results
                
            # Parse CSV output from ratio test
            lines = process.stdout.split('\n')
            csv_started = False
            
            for line in lines:
                if "Producers,Consumers,Total_Time_Sec" in line:
                    csv_started = True
                    continue
                    
                if csv_started and line.strip() and ',' in line:
                    try:
                        parts = line.strip().split(',')
                        if len(parts) >= 5:
                            result = BenchmarkResult()
                            result.language = language
                            result.mode = mode
                            result.threads = int(parts[0]) + int(parts[1])  # producers + consumers
                            result.items = items
                            result.execution_time = float(parts[2])
                            result.messages_per_sec = float(parts[3])
                            result.efficiency = float(parts[4])
                            result.description = f"Ratio test - {parts[0]}P:{parts[1]}C"
                            results.append(result)
                    except (ValueError, IndexError) as e:
                        print(f"    Failed to parse ratio line: {line} ({e})")
                        
        except Exception as e:
            print(f"    Ratio test exception: {e}")
            
        return results
    
    def run_all_benchmarks(self, config: BenchmarkConfig, include_stress: bool = False,
                          include_ratios: bool = False) -> None:
        """Run all benchmark configurations"""
        if not self.check_binaries():
            return
            
        test_cases = config.test_cases.copy()
        if include_stress:
            test_cases.extend(config.get_stress_test_cases())
            
        total_tests = len(test_cases) * len(config.modes) * 2 * config.repeat_count  # 2 languages
        if include_ratios:
            total_tests += len(test_cases) * len(config.modes) * 2  # ratio tests don't repeat
            
        current_test = 0
        
        print(f"Starting comprehensive benchmark suite")
        print(f"Total tests to run: {total_tests}")
        print(f"System: {self.system_info['system']} {self.system_info['machine']}")
        print(f"CPU cores: {self.system_info['cpu_count_logical']} logical, {self.system_info['cpu_count_physical']} physical")
        print(f"Memory: {self.system_info['memory_total_gb']} GB")
        print("=" * 60)
        
        for test_case in test_cases:
            threads = test_case["threads"]
            items = test_case["items"]
            description = test_case["description"]
            
            print(f"\nTest Case: {description}")
            print(f"   Threads: {threads}, Items: {items}")
            
            for mode in config.modes:
                print(f"\n  Mode: {mode}")
                
                # Test both languages
                for binary, language in [(self.rust_binary, "Rust"), (self.cpp_binary, "C++")]:
                    print(f"    {language}:")
                    
                    # Run multiple times for averaging
                    run_results = []
                    for run in range(config.repeat_count):
                        print(f"      Run {run + 1}/{config.repeat_count}:")
                        current_test += 1
                        
                        result = self.run_single_benchmark(binary, language, threads, items, mode)
                        result.description = description
                        
                        if result.error:
                            print(f"        Error: {result.error}")
                        else:
                            print(f"        Time: {result.execution_time:.3f}s, "
                                  f"Efficiency: {result.efficiency:.1f}%, "
                                  f"Msgs/s: {result.messages_per_sec:.0f}")
                        
                        run_results.append(result)
                        
                        # Brief pause between runs
                        time.sleep(0.5)
                    
                    # Calculate average result
                    if run_results and not all(r.error for r in run_results):
                        avg_result = self._calculate_average_result(run_results)
                        self.results.append(avg_result)
                    else:
                        # If all runs failed, add the last error result
                        self.results.append(run_results[-1])
                
                # Run ratio tests if requested
                if include_ratios and threads >= 4:  # Only for tests with enough threads
                    print(f"    Running ratio tests...")
                    for binary, language in [(self.rust_binary, "Rust"), (self.cpp_binary, "C++")]:
                        ratio_results = self.run_ratio_test(binary, language, threads, items // 2, mode)
                        self.results.extend(ratio_results)
                        current_test += 1
        
        print(f"\nBenchmark suite completed!")
        print(f"Total results collected: {len(self.results)}")
    
    def _calculate_average_result(self, results: List[BenchmarkResult]) -> BenchmarkResult:
        """Calculate average of multiple benchmark runs"""
        valid_results = [r for r in results if not r.error]
        if not valid_results:
            return results[0]  # Return the error result
            
        avg = BenchmarkResult()
        avg.language = valid_results[0].language
        avg.mode = valid_results[0].mode
        avg.threads = valid_results[0].threads
        avg.items = valid_results[0].items
        avg.description = valid_results[0].description
        
        # Average the numeric fields
        avg.execution_time = sum(r.execution_time for r in valid_results) / len(valid_results)
        avg.produced = sum(r.produced for r in valid_results) // len(valid_results)
        avg.consumed = sum(r.consumed for r in valid_results) // len(valid_results)
        avg.efficiency = sum(r.efficiency for r in valid_results) / len(valid_results)
        avg.mutex_ops_per_sec = sum(r.mutex_ops_per_sec for r in valid_results) / len(valid_results)
        avg.avg_mutex_time_us = sum(r.avg_mutex_time_us for r in valid_results) / len(valid_results)
        avg.messages_per_sec = sum(r.messages_per_sec for r in valid_results) / len(valid_results)
        avg.peak_memory_mb = sum(r.peak_memory_mb for r in valid_results) / len(valid_results)
        
        return avg
    
    def save_results(self, output_dir: str = "benchmark_results") -> None:
        """Save results to CSV files and generate summary"""
        Path(output_dir).mkdir(exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        
        # Save detailed results
        csv_file = Path(output_dir) / f"benchmark_results_{timestamp}.csv"
        self._save_csv(csv_file)
        
        # Save system info
        info_file = Path(output_dir) / f"system_info_{timestamp}.json"
        with open(info_file, 'w') as f:
            json.dump(self.system_info, f, indent=2)
        
        # Generate summary
        summary_file = Path(output_dir) / f"benchmark_summary_{timestamp}.txt"
        self._generate_summary(summary_file)
        
        print(f"Results saved to:")
        print(f"   Detailed data: {csv_file}")
        print(f"   System info: {info_file}")
        print(f"   Summary: {summary_file}")
    
    def _save_csv(self, filename: Path) -> None:
        """Save results to CSV file"""
        if not self.results:
            print("No results to save")
            return
            
        fieldnames = list(self.results[0].to_dict().keys())
        
        with open(filename, 'w', newline='') as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            writer.writeheader()
            
            for result in self.results:
                writer.writerow(result.to_dict())
    
    def _generate_summary(self, filename: Path) -> None:
        """Generate a human-readable summary"""
        with open(filename, 'w') as f:
            f.write("RUST vs C++ CONCURRENCY BENCHMARK SUMMARY\n")
            f.write("=" * 50 + "\n\n")
            
            f.write(f"Benchmark Date: {self.system_info['timestamp']}\n")
            f.write(f"System: {self.system_info['platform']}\n")
            f.write(f"CPU Cores: {self.system_info['cpu_count_logical']} logical, {self.system_info['cpu_count_physical']} physical\n")
            f.write(f"Memory: {self.system_info['memory_total_gb']} GB\n\n")
            
            # Performance comparison by mode
            for mode in ["channel", "queue"]:
                f.write(f"{mode.upper()} MODE PERFORMANCE:\n")
                f.write("-" * 30 + "\n")
                
                rust_results = [r for r in self.results if r.language == "Rust" and r.mode == mode and not r.error]
                cpp_results = [r for r in self.results if r.language == "C++" and r.mode == mode and not r.error]
                
                if rust_results and cpp_results:
                    rust_avg_time = sum(r.execution_time for r in rust_results) / len(rust_results)
                    cpp_avg_time = sum(r.execution_time for r in cpp_results) / len(cpp_results)
                    rust_avg_throughput = sum(r.messages_per_sec for r in rust_results) / len(rust_results)
                    cpp_avg_throughput = sum(r.messages_per_sec for r in cpp_results) / len(cpp_results)
                    
                    f.write(f"Average Execution Time:\n")
                    f.write(f"  Rust: {rust_avg_time:.3f}s\n")
                    f.write(f"  C++:  {cpp_avg_time:.3f}s\n")
                    f.write(f"  Winner: {'Rust' if rust_avg_time < cpp_avg_time else 'C++'}\n\n")
                    
                    f.write(f"Average Throughput (messages/sec):\n")
                    f.write(f"  Rust: {rust_avg_throughput:.0f}\n")
                    f.write(f"  C++:  {cpp_avg_throughput:.0f}\n")
                    f.write(f"  Winner: {'Rust' if rust_avg_throughput > cpp_avg_throughput else 'C++'}\n\n")
                
                f.write("\n")
            
            # Error summary
            errors = [r for r in self.results if r.error]
            if errors:
                f.write("ERRORS ENCOUNTERED:\n")
                f.write("-" * 20 + "\n")
                for error in errors:
                    f.write(f"{error.language} {error.mode} ({error.threads}t, {error.items}i): {error.error}\n")

def main():
    parser = argparse.ArgumentParser(description="Comprehensive Rust vs C++ concurrency benchmark")
    parser.add_argument("--rust-binary", default="./main", help="Path to Rust binary")
    parser.add_argument("--cpp-binary", default="./main_cpp", help="Path to C++ binary")
    parser.add_argument("--output-dir", default="benchmark_results", help="Output directory for results")
    parser.add_argument("--repeats", type=int, default=3, help="Number of times to repeat each test")
    parser.add_argument("--quick", action="store_true", help="Run only a subset of tests for quick validation")
    
    args = parser.parse_args()
    
    # Create runner
    runner = BenchmarkRunner(args.rust_binary, args.cpp_binary)
    
    # Configure tests
    config = BenchmarkConfig()
    config.repeat_count = args.repeats
    
    try:  # Add this try block to match the except statements
        if args.quick:
            # Quick test subset
            config.test_cases = [
                {"threads": 4, "items": 1000, "description": "Quick test - 4 threads"},
                {"threads": 8, "items": 5000, "description": "Quick test - 8 threads"},
            ]
            config.repeat_count = 1
            # Run only basic tests for quick mode
            runner.run_all_benchmarks(config, include_stress=False, include_ratios=False)
        else:
            # Always run full test suite by default
            runner.run_all_benchmarks(config, include_stress=True, include_ratios=True)
            runner.save_results(args.output_dir)
    except KeyboardInterrupt:
        print("\nBenchmark interrupted by user")
        if runner.results:
            print("Saving partial results...")
            runner.save_results(args.output_dir)
    except Exception as e:
        print(f"Benchmark failed: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()