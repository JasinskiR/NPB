#!/usr/bin/env python3
"""
NAPRAWIONY Comprehensive Benchmark Runner for Rust vs C++ Concurrency Performance
PROBLEM: Poprzedni skrypt używał flag --no-producer-consumer --no-mutex które wyłączały testy Rust!
ROZWIĄZANIE: Usunięte problematyczne flagi + dodany debug output parsera
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
import statistics
import threading
import signal

class BenchmarkConfig:
    """Configuration for benchmark test cases"""
    
    def __init__(self):
        # Set max threads to 8
        self.max_threads = 8
        
        self.test_cases = [
            # Light load tests
            {"threads": 1, "items": 1000, "description": "Light load - 1 thread"},
            {"threads": 2, "items": 2000, "description": "Light load - 2 threads"},
            {"threads": 4, "items": 2000, "description": "Light load - 4 threads"},
            
            # Medium load tests
            {"threads": 4, "items": 5000, "description": "Medium load - 4 threads"},
            {"threads": 6, "items": 5000, "description": "Medium load - 6 threads"},
            {"threads": 8, "items": 5000, "description": "Medium load - 8 threads"},
            
            # Heavy load tests
            {"threads": 8, "items": 10000, "description": "Heavy load - 8 threads"},
            # Remove or cap the 12-thread test to respect max_threads
            # {"threads": 12, "items": 10000, "description": "Heavy load - 12 threads"},
        ]
        
        self.modes = ["channel", "queue"]
        self.repeat_count = 3

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
        self.current_memory_mb = 0.0
        self.description = ""
        self.error = None
        self.run_number = 0
        
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
            "current_memory_mb": self.current_memory_mb,
            "description": self.description,
            "error": self.error,
            "run_number": self.run_number
        }

class FixedBenchmarkRunner:
    """NAPRAWIONY Main benchmark runner class"""
    
    def __init__(self, rust_binary: str = "./rust_benchmark", cpp_binary: str = "./cpp_benchmark"):
        self.rust_binary = rust_binary
        self.cpp_binary = cpp_binary
        self.results: List[BenchmarkResult] = []
        self.system_info = SystemInfo.get_system_info()
        self.interrupted = False
        self.debug_mode = False  # Można włączyć dla debugowania
        signal.signal(signal.SIGINT, self._signal_handler)
        
    def _signal_handler(self, signum, frame):
        """Handle Ctrl+C gracefully"""
        print(f"\nReceived signal {signum}. Gracefully shutting down...")
        self.interrupted = True
        
    def enable_debug(self):
        """Włącz tryb debug z dodatkowymi informacjami"""
        self.debug_mode = True
        
    def check_binaries(self) -> bool:
        """Check if benchmark binaries exist and are executable"""
        rust_exists = Path(self.rust_binary).is_file()
        cpp_exists = Path(self.cpp_binary).is_file()
        
        if not rust_exists:
            print(f"❌ Rust binary not found: {self.rust_binary}")
            print("   Please compile with: cargo build --release")
            
        if not cpp_exists:
            print(f"❌ C++ binary not found: {self.cpp_binary}")
            print("   Please compile with: g++ -O3 -DNDEBUG -std=c++20 -pthread main.cpp -o cpp_benchmark")
            
        return rust_exists and cpp_exists
    
    def run_single_benchmark(self, binary: str, language: str, threads: int, 
                           items: int, mode: str, run_number: int = 1, timeout: int = 300) -> BenchmarkResult:
        """NAPRAWIONY Run a single benchmark and parse results"""
        result = BenchmarkResult()
        result.language = language
        result.mode = mode
        result.threads = threads
        result.items = items
        result.run_number = run_number
        
        try:
            # ⭐ GŁÓWNA NAPRAWA: Usunięte problematyczne flagi!
            if language == "Rust":
                # PRZED (problematyczne): cmd = [..., "--no-producer-consumer", "--no-mutex"]
                # PO (naprawione): Tylko podstawowe parametry
                cmd = [binary, "--threads", str(threads), "--items", str(items), "--mode", mode]
                
                # Dodaj debug info jeśli włączony
                if self.debug_mode:
                    print(f"        🦀 Rust command: {' '.join(cmd)}")
            else:  # C++
                cmd = [binary, "--threads", str(threads), "--items", str(items), "--mode", mode]
                
                if self.debug_mode:
                    print(f"        ⚡ C++ command: {' '.join(cmd)}")
            
            print(f"    Run {run_number}: {language} {mode} {threads}t/{items}i...")
            
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
                result.error = f"Process failed (code {process.returncode}): {process.stderr[:200]}"
                if self.debug_mode:
                    print(f"        ❌ Error output: {process.stderr[:500]}")
                return result
                
            # Parse output with debug info
            output = process.stdout
            if self.debug_mode:
                print(f"        📄 Output preview: {output[:200]}...")
                
            result = self._parse_benchmark_output_enhanced(output, result, language)
            
            # If parsing failed to get execution time, use measured time
            if result.execution_time == 0:
                result.execution_time = actual_time
                if self.debug_mode:
                    print(f"        ⏱️  Used measured time: {actual_time:.3f}s")
                
        except subprocess.TimeoutExpired:
            result.error = f"Timeout after {timeout} seconds"
        except Exception as e:
            result.error = f"Exception: {str(e)}"
            
        return result
    
    def _parse_benchmark_output_enhanced(self, output: str, result: BenchmarkResult, language: str) -> BenchmarkResult:
        """ULEPSZONY Parse benchmark output to extract metrics"""
        lines = output.split('\n')
        metrics_found = []
        
        if self.debug_mode:
            print(f"        📋 Parsing {len(lines)} lines of {language} output...")
        
        for line_num, line in enumerate(lines):
            line = line.strip()
            
            # Parse execution metrics
            if "Total time:" in line:
                match = re.search(r"Total time:\s+([\d.]+)\s+s", line)
                if match:
                    result.execution_time = float(match.group(1))
                    metrics_found.append(f"execution_time={result.execution_time}")
                    
            elif "Produced:" in line and ("(" in line or "/s" in line):
                # Rust format: "Produced: 1000 (1234.56/s)"
                # C++ format might be different
                match = re.search(r"Produced:\s+(\d+).*?\(([\d.]+)/s\)", line)
                if match:
                    result.produced = int(match.group(1))
                    metrics_found.append(f"produced={result.produced}")
                else:
                    # Try simpler pattern
                    match = re.search(r"Produced:\s+(\d+)", line)
                    if match:
                        result.produced = int(match.group(1))
                        metrics_found.append(f"produced={result.produced}")
                    
            elif "Consumed:" in line and ("(" in line or "/s" in line):
                match = re.search(r"Consumed:\s+(\d+).*?\(([\d.]+)/s\)", line)
                if match:
                    result.consumed = int(match.group(1))
                    result.messages_per_sec = float(match.group(2))
                    metrics_found.append(f"consumed={result.consumed}, msg/s={result.messages_per_sec}")
                else:
                    # Try simpler pattern
                    match = re.search(r"Consumed:\s+(\d+)", line)
                    if match:
                        result.consumed = int(match.group(1))
                        metrics_found.append(f"consumed={result.consumed}")
                    
            elif "Efficiency:" in line:
                match = re.search(r"Efficiency:\s+([\d.]+)%", line)
                if match:
                    result.efficiency = float(match.group(1))
                    metrics_found.append(f"efficiency={result.efficiency}%")
                    
            elif "Operations:" in line and "ops/s" in line:
                match = re.search(r"Operations:\s+\d+\s+\(([\d.]+)\s+ops/s\)", line)
                if match:
                    result.mutex_ops_per_sec = float(match.group(1))
                    metrics_found.append(f"mutex_ops/s={result.mutex_ops_per_sec}")
                    
            elif "Avg lock time:" in line or "Avg mutex time:" in line:
                match = re.search(r"Avg.*?time:\s+([\d.]+)\s+[μu]s", line)
                if match:
                    result.avg_mutex_time_us = float(match.group(1))
                    metrics_found.append(f"avg_mutex_time={result.avg_mutex_time_us}μs")
                    
            elif any(keyword in line for keyword in ["Peak memory:", "Peak RSS:", "Current RSS:"]):
                # Try KB format first
                match = re.search(r"(Peak|Current).*?:\s+([\d.]+)\s+KB", line)
                if match:
                    value = float(match.group(2)) / 1024.0  # Convert KB to MB
                    if "Peak" in match.group(1):
                        result.peak_memory_mb = value
                        metrics_found.append(f"peak_memory={value:.2f}MB")
                    else:
                        result.current_memory_mb = value
                        metrics_found.append(f"current_memory={value:.2f}MB")
                else:
                    # Try MB format
                    match = re.search(r"(Peak|Current).*?:\s+([\d.]+)\s+MB", line)
                    if match:
                        value = float(match.group(2))
                        if "Peak" in match.group(1):
                            result.peak_memory_mb = value
                            metrics_found.append(f"peak_memory={value:.2f}MB")
                        else:
                            result.current_memory_mb = value
                            metrics_found.append(f"current_memory={value:.2f}MB")
        
        # Debug output
        if self.debug_mode:
            print(f"        🎯 Metrics found: {', '.join(metrics_found) if metrics_found else 'NONE!'}")
            
            if not metrics_found:
                print(f"        🔍 Sample lines for debugging:")
                for i, line in enumerate(lines[:10]):
                    if line.strip():
                        print(f"        Line {i}: {line.strip()}")
        
        return result
    
    def run_all_benchmarks(self, config: BenchmarkConfig, quick_mode: bool = False) -> None:
        """Run all benchmark configurations"""
        if not self.check_binaries():
            print("\n❌ Cannot proceed without both binaries!")
            print("🔧 Please ensure both Rust and C++ binaries are compiled and accessible.")
            return
            
        test_cases = config.test_cases.copy()
        
        if quick_mode:
            test_cases = test_cases[:3]  # Only first 3 test cases
            config.repeat_count = 1
            print("🏃 Quick mode: Running reduced test suite")
            
        total_tests = len(test_cases) * len(config.modes) * 2 * config.repeat_count
        current_test = 0
        
        print(f"\n🚀 Starting FIXED benchmark suite")
        print(f"📊 Total tests to run: {total_tests}")
        print(f"💻 System: {self.system_info['system']} {self.system_info['machine']}")
        print(f"🔧 CPU cores: {self.system_info['cpu_count_logical']} logical, {self.system_info['cpu_count_physical']} physical")
        print(f"💾 Memory: {self.system_info['memory_total_gb']} GB")
        print("="*70)
        
        for test_idx, test_case in enumerate(test_cases):
            if self.interrupted:
                break
                
            threads = test_case["threads"]
            items = test_case["items"]
            description = test_case["description"]
            
            print(f"\n📋 Test Case {test_idx + 1}/{len(test_cases)}: {description}")
            print(f"   Threads: {threads}, Items: {items}")
            
            for mode in config.modes:
                if self.interrupted:
                    break
                    
                print(f"\n  🔄 Mode: {mode}")
                
                # Test both languages
                for binary, language in [(self.rust_binary, "Rust"), (self.cpp_binary, "C++")]:
                    if self.interrupted:
                        break
                        
                    print(f"    {language}:")
                    
                    # Run multiple times for averaging
                    run_results = []
                    for run in range(config.repeat_count):
                        if self.interrupted:
                            break
                            
                        current_test += 1
                        
                        result = self.run_single_benchmark(binary, language, threads, items, mode, run + 1)
                        result.description = description
                        
                        if result.error:
                            print(f"      ❌ Run {run + 1}: Error: {result.error}")
                        else:
                            print(f"      ✅ Run {run + 1}: Time: {result.execution_time:.3f}s, "
                                  f"Efficiency: {result.efficiency:.1f}%, "
                                  f"Msgs/s: {result.messages_per_sec:.0f}, "
                                  f"Memory: {result.peak_memory_mb:.1f}MB")
                        
                        run_results.append(result)
                        
                        # Brief pause between runs
                        time.sleep(0.2)
                    
                    if self.interrupted:
                        break
                    
                    # Store all individual runs and calculate average
                    for run_result in run_results:
                        self.results.append(run_result)
                    
                    if run_results and not all(r.error for r in run_results):
                        avg_result = self._calculate_average_result(run_results)
                        avg_result.description = f"{description} (average)"
                        self.results.append(avg_result)
                        
                        # Print average
                        print(f"      📊 Average: Time: {avg_result.execution_time:.3f}s, "
                              f"Efficiency: {avg_result.efficiency:.1f}%, "
                              f"Msgs/s: {avg_result.messages_per_sec:.0f}, "
                              f"Memory: {avg_result.peak_memory_mb:.1f}MB")
        
        if self.interrupted:
            print(f"\n⚠️  Benchmark interrupted! Collected {len(self.results)} results before interruption.")
        else:
            print(f"\n🎉 Benchmark suite completed!")
            print(f"📈 Total results collected: {len(self.results)}")
    
    def _calculate_average_result(self, results: List[BenchmarkResult]) -> BenchmarkResult:
        """Calculate average of multiple benchmark runs"""
        valid_results = [r for r in results if not r.error]
        if not valid_results:
            return results[0]
            
        avg = BenchmarkResult()
        avg.language = valid_results[0].language
        avg.mode = valid_results[0].mode
        avg.threads = valid_results[0].threads
        avg.items = valid_results[0].items
        avg.description = valid_results[0].description
        avg.run_number = 0  # 0 indicates average
        
        # Average the numeric fields
        avg.execution_time = statistics.mean(r.execution_time for r in valid_results)
        avg.produced = int(statistics.mean(r.produced for r in valid_results))
        avg.consumed = int(statistics.mean(r.consumed for r in valid_results))
        avg.efficiency = statistics.mean(r.efficiency for r in valid_results)
        avg.mutex_ops_per_sec = statistics.mean(r.mutex_ops_per_sec for r in valid_results)
        avg.avg_mutex_time_us = statistics.mean(r.avg_mutex_time_us for r in valid_results)
        avg.messages_per_sec = statistics.mean(r.messages_per_sec for r in valid_results)
        avg.peak_memory_mb = statistics.mean(r.peak_memory_mb for r in valid_results)
        avg.current_memory_mb = statistics.mean(r.current_memory_mb for r in valid_results)
        
        return avg
    
    def save_results(self, output_dir: str = "benchmark_results") -> None:
        """Save results to CSV files and generate summary"""
        Path(output_dir).mkdir(exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        
        # Save detailed results
        csv_file = Path(output_dir) / f"benchmark_results_FIXED_{timestamp}.csv"
        self._save_csv(csv_file)
        
        # Save system info
        info_file = Path(output_dir) / f"system_info_FIXED_{timestamp}.json"
        with open(info_file, 'w') as f:
            json.dump(self.system_info, f, indent=2)
        
        print(f"\n💾 FIXED Results saved to:")
        print(f"   📊 Detailed data: {csv_file}")
        print(f"   ℹ️  System info: {info_file}")
    
    def _save_csv(self, filename: Path) -> None:
        """Save results to CSV file"""
        if not self.results:
            print("⚠️  No results to save")
            return
            
        fieldnames = list(self.results[0].to_dict().keys())
        
        with open(filename, 'w', newline='', encoding='utf-8') as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            writer.writeheader()
            
            for result in self.results:
                writer.writerow(result.to_dict())
    
    def print_quick_summary(self):
        """Print quick comparison summary"""
        valid_results = [r for r in self.results if not r.error and r.run_number == 0]
        if not valid_results:
            print("⚠️  No valid results for summary")
            return
            
        print("\n" + "="*60)
        print("📊 QUICK COMPARISON SUMMARY (FIXED)")
        print("="*60)
        
        rust_results = [r for r in valid_results if r.language == "Rust"]
        cpp_results = [r for r in valid_results if r.language == "C++"]
        
        if rust_results and cpp_results:
            print(f"\n📈 PERFORMANCE METRICS:")
            
            # Throughput comparison
            rust_avg_throughput = statistics.mean(r.messages_per_sec for r in rust_results)
            cpp_avg_throughput = statistics.mean(r.messages_per_sec for r in cpp_results)
            
            print(f"Average Throughput:")
            print(f"  🦀 Rust: {rust_avg_throughput:.0f} msgs/s")
            print(f"  ⚡ C++:  {cpp_avg_throughput:.0f} msgs/s")
            
            if rust_avg_throughput > 0 and cpp_avg_throughput > 0:
                throughput_winner = "Rust" if rust_avg_throughput > cpp_avg_throughput else "C++"
                throughput_advantage = abs(rust_avg_throughput - cpp_avg_throughput) / max(rust_avg_throughput, cpp_avg_throughput) * 100
                print(f"  🏆 Winner: {throughput_winner} ({throughput_advantage:.1f}% better)")
            
            # Memory comparison
            rust_avg_memory = statistics.mean(r.peak_memory_mb for r in rust_results if r.peak_memory_mb > 0)
            cpp_avg_memory = statistics.mean(r.peak_memory_mb for r in cpp_results if r.peak_memory_mb > 0)
            
            if rust_avg_memory > 0 and cpp_avg_memory > 0:
                print(f"\nAverage Memory Usage:")
                print(f"  🦀 Rust: {rust_avg_memory:.1f} MB")
                print(f"  ⚡ C++:  {cpp_avg_memory:.1f} MB")
                
                memory_winner = "Rust" if rust_avg_memory < cpp_avg_memory else "C++"
                memory_advantage = abs(rust_avg_memory - cpp_avg_memory) / max(rust_avg_memory, cpp_avg_memory) * 100
                print(f"  🏆 Winner: {memory_winner} ({memory_advantage:.1f}% less memory)")
            
            # Time comparison
            rust_avg_time = statistics.mean(r.execution_time for r in rust_results)
            cpp_avg_time = statistics.mean(r.execution_time for r in cpp_results)
            
            print(f"\nAverage Execution Time:")
            print(f"  🦀 Rust: {rust_avg_time*1000:.1f} ms")
            print(f"  ⚡ C++:  {cpp_avg_time*1000:.1f} ms")
            
            time_winner = "Rust" if rust_avg_time < cpp_avg_time else "C++"
            time_advantage = abs(rust_avg_time - cpp_avg_time) / min(rust_avg_time, cpp_avg_time) * 100
            print(f"  🏆 Winner: {time_winner} ({time_advantage:.1f}% faster)")
            
        else:
            print("⚠️  Missing results for one or both languages")
            print(f"Rust results: {len(rust_results)}")
            print(f"C++ results: {len(cpp_results)}")

def main():
    parser = argparse.ArgumentParser(
        description="FIXED Rust vs C++ concurrency benchmark",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
🔧 GŁÓWNE NAPRAWY W TEJ WERSJI:
- Usunięte problematyczne flagi --no-producer-consumer --no-mutex dla Rust
- Ulepszony parser output-u z debug mode
- Dodane szczegółowe informacje o błędach parsowania

Examples:
  python fixed_benchmark.py --quick                    # Quick test with fixed flags
  python fixed_benchmark.py --debug                    # Enable debug output
  python fixed_benchmark.py --rust-binary ./rust_bin --cpp-binary ./cpp_bin
        """
    )
    
    parser.add_argument("--rust-binary", default="./rust_benchmark", 
                       help="Path to Rust binary")
    parser.add_argument("--cpp-binary", default="./cpp_benchmark", 
                       help="Path to C++ binary")
    parser.add_argument("--output-dir", default="benchmark_results", 
                       help="Output directory for results")
    parser.add_argument("--repeats", type=int, default=3, 
                       help="Number of times to repeat each test")
    parser.add_argument("--quick", action="store_true", 
                       help="Run only a subset of tests for quick validation")
    parser.add_argument("--debug", action="store_true", 
                       help="Enable debug mode with detailed parsing info")
    parser.add_argument("--timeout", type=int, default=300, 
                       help="Timeout for individual tests in seconds")
    
    args = parser.parse_args()
    
    # Create fixed runner
    runner = FixedBenchmarkRunner(args.rust_binary, args.cpp_binary)
    
    if args.debug:
        runner.enable_debug()
        print("🐛 Debug mode enabled - will show detailed parsing information")
    
    # Configure tests
    config = BenchmarkConfig()
    config.repeat_count = args.repeats
    
    # Apply max threads constraint
    max_threads = 8  # Hard-coded maximum
    for test_case in config.test_cases[:]:
        if test_case["threads"] > max_threads:
            config.test_cases.remove(test_case)
            print(f"⚠️ Removed test case using {test_case['threads']} threads (max: {max_threads})")
    
    try:
        print("🔧 NAPRAWIONY BENCHMARK RUNNER")
        print("Główne zmiany:")
        print("  ✅ Usunięte flagi --no-producer-consumer --no-mutex")
        print("  ✅ Ulepszony parser z debug mode")
        print("  ✅ Lepsze raportowanie błędów")
        print()
        
        runner.run_all_benchmarks(config, quick_mode=args.quick)
        
        if runner.results:
            runner.save_results(args.output_dir)
            runner.print_quick_summary()
            
            print("\n🎯 NASTĘPNE KROKI:")
            print("1. Uruchom analyze.py na nowym pliku CSV")
            print("2. Porównaj wyniki z poprzednimi")
            print("3. Sprawdź czy Rust teraz pokazuje prawdziwe dane")
        else:
            print("⚠️  No results to save - all tests may have failed")
            
    except KeyboardInterrupt:
        print("\n🛑 Benchmark interrupted by user")
        if runner.results:
            print("💾 Saving partial results...")
            runner.save_results(args.output_dir)
    except Exception as e:
        print(f"❌ Benchmark failed: {e}")
        if runner.results:
            print("💾 Saving partial results...")
            runner.save_results(args.output_dir)
        sys.exit(1)

if __name__ == "__main__":
    main()