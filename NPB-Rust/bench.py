#!/usr/bin/env python3

import os
import sys
import json
import subprocess
import platform
import time
import csv
from pathlib import Path
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple
import re

@dataclass
class BenchmarkResult:
    class_name: str
    threads: int
    iteration: int
    execution_time: float
    reported_time: float
    mflops: float
    verification_passed: bool
    memory_delta_kb: int
    timestamp: str
    exit_code: int
    class_npb_reported: str
    size_reported: str
    total_threads_reported: int
    iterations_reported: int
    operation_type: str
    version_npb: str

@dataclass
class SystemInfo:
    hostname: str
    os: str
    arch: str
    kernel: str
    cpu_model: str
    total_cores: int
    memory_gb: float

@dataclass
class QualityMetrics:
    loc_rust: int = 0
    clippy_warnings: int = 0
    clippy_errors: int = 0
    binary_size_mb: float = 0.0

class UnifiedNPBAnalyzer:
    def __init__(self, binary_path: str, output_dir: str):
        self.binary_path = Path(binary_path)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        self.os_type = platform.system().lower()
        self.core_distribution_data = []
        self.perf_data = []
        self.benchmark_results = []
        
    def run_command(self, cmd: List[str], timeout: int = 60) -> Tuple[int, str, str]:
        try:
            result = subprocess.run(
                cmd, 
                capture_output=True, 
                text=True, 
                timeout=timeout
            )
            return result.returncode, result.stdout, result.stderr
        except subprocess.TimeoutExpired:
            return -1, "", "Command timed out"
        except Exception as e:
            return -1, "", str(e)
    
    def get_system_info(self) -> SystemInfo:
        hostname = os.environ.get('HOSTNAME', os.environ.get('COMPUTERNAME', 'unknown'))
        os_name = platform.system()
        arch = platform.machine()
        kernel = platform.release()
        
        cpu_model = "Unknown CPU"
        total_cores = 1
        memory_gb = 0.0
        
        if os_name == "Darwin":
            exit_code, stdout, _ = self.run_command(['sysctl', '-n', 'machdep.cpu.brand_string'])
            if exit_code == 0:
                cpu_model = stdout.strip()
            
            exit_code, stdout, _ = self.run_command(['sysctl', '-n', 'hw.ncpu'])
            if exit_code == 0:
                total_cores = int(stdout.strip())
            
            exit_code, stdout, _ = self.run_command(['sysctl', '-n', 'hw.memsize'])
            if exit_code == 0:
                memory_gb = int(stdout.strip()) / (1024**3)
                
        elif os_name == "Linux":
            try:
                with open('/proc/cpuinfo', 'r') as f:
                    for line in f:
                        if line.startswith('model name'):
                            cpu_model = line.split(':', 1)[1].strip()
                            break
            except:
                pass
            
            exit_code, stdout, _ = self.run_command(['nproc'])
            if exit_code == 0:
                total_cores = int(stdout.strip())
            
            try:
                with open('/proc/meminfo', 'r') as f:
                    for line in f:
                        if line.startswith('MemTotal:'):
                            memory_kb = int(line.split()[1])
                            memory_gb = memory_kb / (1024**2)
                            break
            except:
                pass
        
        return SystemInfo(
            hostname=hostname,
            os=os_name,
            arch=arch,
            kernel=kernel,
            cpu_model=cpu_model,
            total_cores=total_cores,
            memory_gb=memory_gb
        )
    
    def get_memory_usage(self) -> int:
        if self.os_type == 'linux':
            try:
                result = subprocess.run(['ps', '-o', 'rss=', '-p', str(os.getpid())], 
                                      capture_output=True, text=True)
                if result.returncode == 0:
                    return int(result.stdout.strip())
            except:
                pass
        return 0
    
    def detect_benchmark_type(self) -> str:
        """
        Detect benchmark type from binary path.
        
        Returns:
            'is', 'ep', 'cg', or 'unknown'
        """
        binary_name = self.binary_path.name.lower()
        
        if 'is' in binary_name:
            return 'is'
        elif 'ep' in binary_name:
            return 'ep'
        elif 'cg' in binary_name:
            return 'cg'
        elif 'mg' in binary_name:
            return 'mg'
        elif 'ft' in binary_name:
            return 'ft'
        elif 'bt' in binary_name:
            return 'bt'
        elif 'sp' in binary_name:
            return 'sp'
        elif 'lu' in binary_name:
            return 'lu'
        else:
            return 'unknown'
    
    def parse_is_output(self, output: str) -> Tuple[bool, float, float, str, str, int, int, str, str]:
        """
        Parser for IS (Integer Sort) benchmark output.
        
        IS-specific patterns:
        - Verification: "Verification    =               SUCCESSFUL"
        - Performance: "Mop/s total     = X.XX" (Mega Operations per second)
        - Fields: "class_npb       =", "Total threads   ="
        """
        verification = False
        mops = 0.0  # IS uses Mop/s (Mega Operations per second)
        time_val = 0.0
        class_npb_reported = "N/A"
        size_reported = "N/A"
        total_threads_reported = 0
        iterations_reported = 0
        operation_type = "N/A"
        version_npb = "N/A"
        
        # Check for IS verification pattern (flexible spacing)
        if "Verification" in output and "SUCCESSFUL" in output:
            for line in output.split('\n'):
                if "Verification" in line and "SUCCESSFUL" in line:
                    verification = True
                    break
        
        for line in output.split('\n'):
            line = line.strip()
            
            # Performance metrics - IS uses "Mop/s total     ="
            if 'Mop/s total' in line and '=' in line:
                try:
                    mops = float(line.split('=')[1].strip())
                except:
                    pass
            elif 'Time in seconds' in line and '=' in line:
                try:
                    time_val = float(line.split('=')[1].strip())
                except:
                    pass
            elif 'class_npb' in line and '=' in line:
                try:
                    class_npb_reported = line.split('=')[1].strip()
                except:
                    pass
            elif 'Size' in line and '=' in line and 'class' not in line:
                try:
                    size_reported = line.split('=')[1].strip()
                except:
                    pass
            elif 'Total threads' in line and '=' in line:
                try:
                    total_threads_reported = int(line.split('=')[1].strip())
                except:
                    pass
            elif 'Iterations' in line and '=' in line:
                try:
                    iterations_reported = int(line.split('=')[1].strip())
                except:
                    pass
            elif 'Operation type' in line and '=' in line:
                try:
                    operation_type = line.split('=')[1].strip()
                except:
                    pass
            elif 'Version' in line and '=' in line:
                try:
                    version_npb = line.split('=')[1].strip()
                except:
                    pass
        
        return (verification, mops, time_val, class_npb_reported, size_reported,
                total_threads_reported, iterations_reported, operation_type, version_npb)

    def parse_ep_output(self, output: str) -> Tuple[bool, float, float, str, str, int, int, str, str]:
        """
        Parser for EP (Embarrassingly Parallel) benchmark output.
        
        EP-specific patterns:
        - Verification: "Verification    =               SUCCESSFUL"
        - Performance: "Mop/s total     = X.XX"
        - Fields: "class_npb       =", "Total threads   ="
        """
        verification = False
        mflops = 0.0
        time_val = 0.0
        class_npb_reported = "N/A"
        size_reported = "N/A"
        total_threads_reported = 0
        iterations_reported = 0
        operation_type = "N/A"
        version_npb = "N/A"
        
        # Check for EP verification pattern (flexible spacing)
        if "Verification" in output and "SUCCESSFUL" in output:
            for line in output.split('\n'):
                if "Verification" in line and "SUCCESSFUL" in line:
                    verification = True
                    break
        
        for line in output.split('\n'):
            line = line.strip()
            
            # Performance metrics - EP uses "Mop/s total     ="
            if 'Mop/s total' in line and '=' in line:
                try:
                    mflops = float(line.split('=')[1].strip())
                except:
                    pass
            elif 'Time in seconds' in line and '=' in line:
                try:
                    time_val = float(line.split('=')[1].strip())
                except:
                    pass
            elif 'class_npb' in line and '=' in line:
                try:
                    class_npb_reported = line.split('=')[1].strip()
                except:
                    pass
            elif 'Size' in line and '=' in line and 'class' not in line:
                try:
                    size_reported = line.split('=')[1].strip()
                except:
                    pass
            elif 'Total threads' in line and '=' in line:
                try:
                    total_threads_reported = int(line.split('=')[1].strip())
                except:
                    pass
            elif 'Iterations' in line and '=' in line:
                try:
                    iterations_reported = int(line.split('=')[1].strip())
                except:
                    pass
            elif 'Operation type' in line and '=' in line:
                try:
                    operation_type = line.split('=')[1].strip()
                except:
                    pass
            elif 'Version' in line and '=' in line:
                try:
                    version_npb = line.split('=')[1].strip()
                except:
                    pass
        
        return (verification, mflops, time_val, class_npb_reported, size_reported,
                total_threads_reported, iterations_reported, operation_type, version_npb)

    def parse_cg_output(self, output: str) -> Tuple[bool, float, float, str, str, int, int, str, str]:
        """
        Parser for CG (Conjugate Gradient) benchmark output.
        
        CG-specific patterns:
        - Verification: "Verification    =               SUCCESSFUL"
        - Performance: "Mop/s total     = X.XX"
        - Fields: "class_npb       =", "Total threads   ="
        """
        verification = False
        mflops = 0.0
        time_val = 0.0
        class_npb_reported = "N/A"
        size_reported = "N/A"
        total_threads_reported = 0
        iterations_reported = 0
        operation_type = "N/A"
        version_npb = "N/A"
        
        # Check for CG verification pattern (flexible spacing)
        if "Verification" in output and "SUCCESSFUL" in output:
            for line in output.split('\n'):
                if "Verification" in line and "SUCCESSFUL" in line:
                    verification = True
                    break
        
        for line in output.split('\n'):
            line = line.strip()
            
            # Performance metrics - CG uses "Mop/s total     ="
            if 'Mop/s total' in line and '=' in line:
                try:
                    mflops = float(line.split('=')[1].strip())
                except:
                    pass
            elif 'Time in seconds' in line and '=' in line:
                try:
                    time_val = float(line.split('=')[1].strip())
                except:
                    pass
            elif 'class_npb' in line and '=' in line:
                try:
                    class_npb_reported = line.split('=')[1].strip()
                except:
                    pass
            elif 'Size' in line and '=' in line and 'class' not in line:
                try:
                    size_reported = line.split('=')[1].strip()
                except:
                    pass
            elif 'Total threads' in line and '=' in line:
                try:
                    total_threads_reported = int(line.split('=')[1].strip())
                except:
                    pass
            elif 'Iterations' in line and '=' in line:
                try:
                    iterations_reported = int(line.split('=')[1].strip())
                except:
                    pass
            elif 'Operation type' in line and '=' in line:
                try:
                    operation_type = line.split('=')[1].strip()
                except:
                    pass
            elif 'Version' in line and '=' in line:
                try:
                    version_npb = line.split('=')[1].strip()
                except:
                    pass
        
        return (verification, mflops, time_val, class_npb_reported, size_reported,
                total_threads_reported, iterations_reported, operation_type, version_npb)

    def parse_output_with_correct_parser(self, output: str, benchmark_type: str) -> Tuple[bool, float, float, str, str, int, int, str, str]:
        """
        Routes to the appropriate parser based on benchmark type.
        
        Args:
            output: Raw benchmark output
            benchmark_type: Detected benchmark type ('is', 'ep', 'cg', etc.)
            
        Returns:
            Parsed benchmark data tuple
        """
        if benchmark_type == 'is':
            return self.parse_is_output(output)
        elif benchmark_type == 'ep':
            return self.parse_ep_output(output)
        elif benchmark_type == 'cg':
            return self.parse_cg_output(output)
        else:
            # Default to EP parser for unknown benchmarks (most compatible)
            print(f"  ⚠ Unknown benchmark type '{benchmark_type}', using EP parser")
            return self.parse_ep_output(output)
    
    def run_single_benchmark(self, class_name: str, threads: int, iteration: int) -> Optional[BenchmarkResult]:
        start_time = time.time()
        memory_before = self.get_memory_usage()
        
        try:
            result = subprocess.run(
                [str(self.binary_path), class_name, str(threads)],
                capture_output=True,
                text=True,
                timeout=300
            )
            
            end_time = time.time()
            memory_after = self.get_memory_usage()
            execution_time = end_time - start_time
            
            if result.returncode != 0:
                print(f"    ERROR: Benchmark failed with exit code {result.returncode}")
                return None
            
            # Auto-detect benchmark type and use appropriate parser
            benchmark_type = self.detect_benchmark_type()
            parsed = self.parse_output_with_correct_parser(result.stdout, benchmark_type)
            verification, mflops, reported_time, class_npb_reported, size_reported, \
            total_threads_reported, iterations_reported, operation_type, version_npb = parsed
            
            memory_delta = max(0, memory_after - memory_before)
            timestamp = time.strftime('%Y-%m-%dT%H:%M:%S.%fZ')
            
            return BenchmarkResult(
                class_name=class_name,
                threads=threads,
                iteration=iteration,
                execution_time=execution_time,
                reported_time=reported_time,
                mflops=mflops,
                verification_passed=verification,
                memory_delta_kb=memory_delta,
                timestamp=timestamp,
                exit_code=result.returncode,
                class_npb_reported=class_npb_reported,
                size_reported=size_reported,
                total_threads_reported=total_threads_reported,
                iterations_reported=iterations_reported,
                operation_type=operation_type,
                version_npb=version_npb
            )
            
        except subprocess.TimeoutExpired:
            print(f"    TIMEOUT: Benchmark timed out")
            return None
        except Exception as e:
            print(f"    ERROR: {str(e)}")
            return None
    
    def run_basic_benchmarks(self, test_classes: str, max_threads: int, iterations: int):
        print("🚀 Running basic performance benchmarks...")
        
        # Detect benchmark type and report it
        benchmark_type = self.detect_benchmark_type()
        print(f"📋 Detected benchmark type: {benchmark_type.upper()}")
        
        thread_counts = list(range(1, max_threads + 1))
        if max_threads not in [1, 2, 4, 8]:
            thread_counts.append(max_threads)
        thread_counts = sorted(set(thread_counts))
        
        total_runs = len(test_classes) * len(thread_counts) * iterations
        current_run = 0
        
        print(f"Thread counts to test: {thread_counts}")
        print(f"Starting {total_runs} benchmark runs...")
        print()
        
        for class_char in test_classes:
            print(f"=== Testing Class {class_char} ===")
            
            for thread_count in thread_counts:
                print(f"  Threads: {thread_count}")
                
                for iteration in range(1, iterations + 1):
                    current_run += 1
                    print(f"    Iteration {iteration}/{iterations} (Run {current_run}/{total_runs}): ", end="")
                    
                    result = self.run_single_benchmark(class_char, thread_count, iteration)
                    
                    if result:
                        self.benchmark_results.append(result)
                        if result.verification_passed:
                            print(f"✓ {result.execution_time:.3f}s, {result.mflops:.2f} MFLOPS")
                        else:
                            print(f"✗ {result.execution_time:.3f}s, {result.mflops:.2f} MFLOPS (FAILED VERIFICATION)")
                    else:
                        print("✗ FAILED")
                
                print()
    
    def save_benchmark_results(self):
        json_path = self.output_dir / 'benchmark_results.json'
        with open(json_path, 'w') as f:
            json_data = []
            for result in self.benchmark_results:
                json_data.append({
                    "class": result.class_name,
                    "threads": result.threads,
                    "iteration": result.iteration,
                    "execution_time_seconds": result.execution_time,
                    "reported_time_seconds": result.reported_time,
                    "mflops": result.mflops,
                    "verification_passed": result.verification_passed,
                    "memory_delta_kb": result.memory_delta_kb,
                    "timestamp": result.timestamp,
                    "exit_code": result.exit_code,
                    "class_npb_reported": result.class_npb_reported,
                    "size_reported": result.size_reported,
                    "total_threads_reported": result.total_threads_reported,
                    "iterations_reported": result.iterations_reported,
                    "operation_type": result.operation_type,
                    "version_npb": result.version_npb
                })
            json.dump(json_data, f, indent=2)
        
        csv_path = self.output_dir / 'benchmark_results.csv'
        with open(csv_path, 'w', newline='') as f:
            fieldnames = [
                'class', 'threads', 'iteration', 'execution_time_seconds', 'reported_time_seconds',
                'mflops', 'verification_passed', 'memory_delta_kb', 'timestamp', 'exit_code',
                'class_npb_reported', 'size_reported', 'total_threads_reported', 
                'iterations_reported', 'operation_type', 'version_npb'
            ]
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            
            for result in self.benchmark_results:
                writer.writerow({
                    'class': result.class_name,
                    'threads': result.threads,
                    'iteration': result.iteration,
                    'execution_time_seconds': result.execution_time,
                    'reported_time_seconds': result.reported_time,
                    'mflops': result.mflops,
                    'verification_passed': result.verification_passed,
                    'memory_delta_kb': result.memory_delta_kb,
                    'timestamp': result.timestamp,
                    'exit_code': result.exit_code,
                    'class_npb_reported': result.class_npb_reported,
                    'size_reported': result.size_reported,
                    'total_threads_reported': result.total_threads_reported,
                    'iterations_reported': result.iterations_reported,
                    'operation_type': result.operation_type,
                    'version_npb': result.version_npb
                })
        
        print(f"  ✓ Benchmark results saved to {json_path}")
        print(f"  ✓ Benchmark results saved to {csv_path}")
    
    def get_hwloc_topology(self) -> Dict:
        topology = {}
        
        exit_code, _, _ = self.run_command(['which', 'hwloc-info'])
        if exit_code != 0:
            print("  ⚠ hwloc not available, using basic topology info")
            return topology
        
        print("  🏗️ Analyzing hardware topology with hwloc...")
        
        exit_code, stdout, stderr = self.run_command(['hwloc-info'])
        if exit_code == 0:
            topology['hwloc_info'] = stdout
            
            for line in stdout.split('\n'):
                if 'Core' in line and 'depth' in line:
                    try:
                        parts = line.split()
                        for part in parts:
                            if part.isdigit():
                                topology['cores'] = int(part)
                                break
                    except:
                        pass
                elif 'PU' in line and 'depth' in line:
                    try:
                        parts = line.split()
                        for part in parts:
                            if part.isdigit():
                                topology['threads'] = int(part)
                                break
                    except:
                        pass
        
        exit_code, _, _ = self.run_command(['which', 'lstopo'])
        if exit_code == 0:
            print("    📊 Generating topology visualization...")
            
            exit_code, _, _ = self.run_command([
                'lstopo', '--of', 'svg', str(self.output_dir / 'topology.svg')
            ])
            if exit_code == 0:
                print("      ✓ Generated topology.svg")
        
        return topology
    
    def analyze_code_quality(self) -> QualityMetrics:
        metrics = QualityMetrics()
        
        print("🔍 Analyzing code quality...")
        
        rust_files = list(Path('.').glob('**/*.rs'))
        total_lines = 0
        for file in rust_files:
            try:
                with open(file, 'r', encoding='utf-8') as f:
                    lines = len([line for line in f if line.strip() and not line.strip().startswith('//')])
                    total_lines += lines
            except:
                continue
        
        metrics.loc_rust = total_lines
        print(f"  📊 Rust LOC: {metrics.loc_rust}")
        
        print("  📎 Running Clippy analysis...")
        exit_code, stdout, stderr = self.run_command(['cargo', 'clippy', '--all-targets'])
        
        output = stdout + stderr
        metrics.clippy_warnings = output.count('warning:')
        metrics.clippy_errors = output.count('error:')
        
        print(f"    ✓ Clippy warnings: {metrics.clippy_warnings}, errors: {metrics.clippy_errors}")
        
        if self.binary_path.exists():
            metrics.binary_size_mb = self.binary_path.stat().st_size / (1024 * 1024)
            print(f"  📏 Binary size: {metrics.binary_size_mb:.2f} MB")
        
        return metrics
    
    def test_thread_affinity(self, test_classes: str, max_threads: int, iterations: int) -> Dict:
        affinity_results = {}
        
        exit_code, _, _ = self.run_command(['which', 'hwloc-bind'])
        if exit_code != 0:
            print("  ⚠ hwloc-bind not available, skipping affinity analysis")
            return affinity_results
        
        print("🧵 Testing thread affinity...")
        
        strategies = [
            ('default', None),
            ('socket_0', 'socket:0'),
            ('core_0_1', '0-1'),
            ('single_core', '0'),
        ]
        
        affinity_csv_data = []
        
        for class_char in test_classes:
            print(f"  Testing class {class_char}...")
            class_results = {}
            
            for strategy_name, binding in strategies:
                times = []
                
                for iteration in range(iterations):
                    if binding:
                        cmd = ['hwloc-bind', binding, str(self.binary_path), class_char, str(max_threads)]
                    else:
                        cmd = [str(self.binary_path), class_char, str(max_threads)]
                    
                    start_time = time.time()
                    exit_code, stdout, stderr = self.run_command(cmd)
                    end_time = time.time()
                    
                    if exit_code == 0:
                        exec_time = end_time - start_time
                        times.append(exec_time)
                        
                        mflops = 0.0
                        for line in stdout.split('\n'):
                            if 'Mop/s total' in line:
                                try:
                                    mflops = float(line.split('=')[1].strip())
                                except:
                                    pass
                        
                        affinity_csv_data.append({
                            'class': class_char,
                            'strategy': strategy_name,
                            'binding': binding if binding else 'default',
                            'iteration': iteration + 1,
                            'execution_time': exec_time,
                            'mflops': mflops,
                            'threads': max_threads
                        })
                
                if times:
                    avg_time = sum(times) / len(times)
                    class_results[strategy_name] = avg_time
                    print(f"    {strategy_name}: {avg_time:.3f}s")
            
            if class_results:
                affinity_results[class_char] = class_results
        
        if affinity_csv_data:
            csv_path = self.output_dir / 'thread_affinity_results.csv'
            with open(csv_path, 'w', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=['class', 'strategy', 'binding', 'iteration', 'execution_time', 'mflops', 'threads'])
                writer.writeheader()
                writer.writerows(affinity_csv_data)
            print(f"  ✓ Thread affinity data saved to {csv_path}")
        
        return affinity_results
    
    def run_performance_profiling(self, test_classes: str, max_threads: int) -> Dict:
        profiling_results = {}
        
        print("🚀 Running detailed performance profiling...")
        
        if self.os_type == 'linux':
            profiling_results = self._profile_linux(test_classes, max_threads)
        elif self.os_type == 'darwin':
            profiling_results = self._profile_macos(test_classes, max_threads)
        else:
            print(f"  ⚠ Performance profiling not supported on {self.os_type}")
        
        if self.perf_data:
            csv_path = self.output_dir / 'performance_profiling.csv'
            with open(csv_path, 'w', newline='') as f:
                if self.perf_data:
                    fieldnames = ['class', 'metric', 'value', 'unit']
                    writer = csv.DictWriter(f, fieldnames=fieldnames)
                    writer.writeheader()
                    writer.writerows(self.perf_data)
            print(f"  ✓ Performance profiling data saved to {csv_path}")
        
        return profiling_results
    
    def _profile_linux(self, test_classes: str, max_threads: int) -> Dict:
        results = {}
        
        exit_code, _, _ = self.run_command(['which', 'perf'])
        if exit_code != 0:
            print("  ⚠ perf not available, skipping detailed profiling")
            return results
        
        print("  🐧 Using perf for detailed profiling...")
        
        for class_char in test_classes:
            print(f"    Profiling class {class_char}...")
            class_results = {}
            
            cmd = [
                'perf', 'stat', 
                '-e', 'cycles,instructions,cache-misses,cache-references,branches,branch-misses',
                str(self.binary_path), class_char, str(max_threads)
            ]
            
            exit_code, stdout, stderr = self.run_command(cmd)
            if exit_code == 0:
                perf_data = self._parse_perf_output(stderr)
                class_results['perf_counters'] = perf_data
                
                for metric, value in perf_data.items():
                    if isinstance(value, (int, float)):
                        self.perf_data.append({
                            'class': class_char,
                            'metric': metric,
                            'value': value,
                            'unit': self._get_metric_unit(metric)
                        })
                
                print(f"      ✓ Perf counters collected for class {class_char}")
            
            if class_results:
                results[class_char] = class_results
        
        return results
    
    def _profile_macos(self, test_classes: str, max_threads: int) -> Dict:
        results = {}
        
        exit_code, _, _ = self.run_command(['which', 'instruments'])
        if exit_code != 0:
            print("  ⚠ instruments not available, using basic profiling")
            return self._profile_macos_basic(test_classes, max_threads)
        
        print("  🍎 Using instruments for detailed profiling...")
        
        first_class = test_classes[0] if test_classes else 'S'
        print(f"    Profiling class {first_class} with instruments...")
        
        class_results = {}
        
        trace_file = self.output_dir / f'time_profiler_{first_class}.trace'
        cmd = [
            'instruments', '-t', 'Time Profiler',
            '-D', str(trace_file),
            str(self.binary_path), first_class, str(max_threads)
        ]
        
        exit_code, stdout, stderr = self.run_command(cmd, timeout=120)
        if exit_code == 0:
            class_results['time_profiler'] = str(trace_file)
            print(f"      ✓ Time Profiler trace saved")
        
        if class_results:
            results[first_class] = class_results
            print("      📝 Open .trace files in Instruments.app for detailed analysis")
        
        return results
    
    def _profile_macos_basic(self, test_classes: str, max_threads: int) -> Dict:
        results = {}
        
        print("  ⏱️ Using basic time profiling...")
        
        for class_char in test_classes:
            cmd = ['/usr/bin/time', '-l', str(self.binary_path), class_char, str(max_threads)]
            
            exit_code, stdout, stderr = self.run_command(cmd)
            if exit_code == 0:
                time_data = self._parse_macos_time(stderr)
                results[class_char] = {'time_stats': time_data}
                
                for metric, value in time_data.items():
                    if isinstance(value, (int, float)):
                        self.perf_data.append({
                            'class': class_char,
                            'metric': metric,
                            'value': value,
                            'unit': self._get_metric_unit(metric)
                        })
                
                print(f"    ✓ Time profiling for class {class_char}")
        
        return results
    
    def _get_metric_unit(self, metric: str) -> str:
        units = {
            'cycles': 'count',
            'instructions': 'count',
            'cache_misses': 'count',
            'cache_references': 'count',
            'branches': 'count',
            'branch_misses': 'count',
            'ipc': 'ratio',
            'cache_miss_rate': 'ratio',
            'max_memory_bytes': 'bytes',
            'page_reclaims': 'count',
            'page_faults': 'count'
        }
        return units.get(metric, 'count')
    
    def _parse_perf_output(self, perf_output: str) -> Dict:
        data = {}
        
        for line in perf_output.split('\n'):
            line = line.strip()
            if 'cycles' in line and not 'cpu-cycles' in line:
                try:
                    data['cycles'] = int(line.split()[0].replace(',', ''))
                except:
                    pass
            elif 'instructions' in line:
                try:
                    data['instructions'] = int(line.split()[0].replace(',', ''))
                except:
                    pass
            elif 'cache-misses' in line:
                try:
                    data['cache_misses'] = int(line.split()[0].replace(',', ''))
                except:
                    pass
            elif 'cache-references' in line:
                try:
                    data['cache_references'] = int(line.split()[0].replace(',', ''))
                except:
                    pass
            elif 'branches' in line and 'branch-misses' not in line:
                try:
                    data['branches'] = int(line.split()[0].replace(',', ''))
                except:
                    pass
            elif 'branch-misses' in line:
                try:
                    data['branch_misses'] = int(line.split()[0].replace(',', ''))
                except:
                    pass
        
        if 'cycles' in data and 'instructions' in data and data['cycles'] > 0:
            data['ipc'] = data['instructions'] / data['cycles']
        
        if 'cache_misses' in data and 'cache_references' in data and data['cache_references'] > 0:
            data['cache_miss_rate'] = data['cache_misses'] / data['cache_references']
        
        return data
    
    def _parse_macos_time(self, time_output: str) -> Dict:
        data = {}
        
        for line in time_output.split('\n'):
            line = line.strip()
            if 'maximum resident set size' in line:
                try:
                    data['max_memory_bytes'] = int(line.split()[0])
                except:
                    pass
            elif 'page reclaims' in line:
                try:
                    data['page_reclaims'] = int(line.split()[0])
                except:
                    pass
            elif 'page faults' in line:
                try:
                    data['page_faults'] = int(line.split()[0])
                except:
                    pass
        
        return data
    
    def generate_summary_stats(self):
        if not self.benchmark_results:
            return
        
        successful_results = [r for r in self.benchmark_results if r.verification_passed]
        total_results = len(self.benchmark_results)
        successful_count = len(successful_results)
        failed_count = total_results - successful_count
        
        print("\n=== BENCHMARK SUMMARY ===")
        print(f"Total runs: {total_results}")
        print(f"Successful verifications: {successful_count}")
        print(f"Failed verifications: {failed_count}")
        
        if successful_results:
            times = [r.execution_time for r in successful_results]
            mflops = [r.mflops for r in successful_results]
            
            min_time = min(times)
            max_time = max(times)
            avg_time = sum(times) / len(times)
            
            min_mflops = min(mflops)
            max_mflops = max(mflops)
            avg_mflops = sum(mflops) / len(mflops)
            
            print(f"Execution time: {min_time:.3f}s (min) / {avg_time:.3f}s (avg) / {max_time:.3f}s (max)")
            print(f"MFLOPS: {min_mflops:.2f} (min) / {avg_mflops:.2f} (avg) / {max_mflops:.2f} (max)")
    
    def generate_comprehensive_report(self, system_info: SystemInfo, topology: Dict, 
                                    quality_metrics: QualityMetrics, profiling_results: Dict, 
                                    affinity_results: Dict, test_config: Dict):
        print("📝 Generating comprehensive analysis report...")
        
        report_data = {
            'metadata': {
                'timestamp': time.strftime('%Y-%m-%d %H:%M:%S'),
                'test_classes': test_config['classes'],
                'max_threads': test_config['max_threads'],
                'iterations': test_config['iterations'],
                'binary_path': str(self.binary_path)
            },
            'system_info': {
                'hostname': system_info.hostname,
                'os': system_info.os,
                'arch': system_info.arch,
                'kernel': system_info.kernel,
                'cpu_model': system_info.cpu_model,
                'total_cores': system_info.total_cores,
                'memory_gb': system_info.memory_gb
            },
            'hardware_topology': topology,
            'code_quality': {
                'rust_loc': quality_metrics.loc_rust,
                'clippy_warnings': quality_metrics.clippy_warnings,
                'clippy_errors': quality_metrics.clippy_errors,
                'binary_size_mb': quality_metrics.binary_size_mb
            },
            'thread_affinity_summary': affinity_results,
            'performance_profiling_summary': profiling_results,
            'benchmark_summary': self._generate_benchmark_summary()
        }
        
        with open(self.output_dir / 'comprehensive_analysis.json', 'w') as f:
            json.dump(report_data, f, indent=2)
        
        self._generate_markdown_report(report_data)
        
        print(f"    ✓ Comprehensive report saved to {self.output_dir}/comprehensive_analysis.json")
        print(f"    ✓ Markdown report saved to {self.output_dir}/analysis_report.md")
    
    def _generate_benchmark_summary(self) -> Dict:
        if not self.benchmark_results:
            return {}
        
        successful_results = [r for r in self.benchmark_results if r.verification_passed]
        
        by_class = {}
        for result in successful_results:
            if result.class_name not in by_class:
                by_class[result.class_name] = []
            by_class[result.class_name].append(result)
        
        summary = {
            'total_runs': len(self.benchmark_results),
            'successful_runs': len(successful_results),
            'failed_runs': len(self.benchmark_results) - len(successful_results),
            'by_class': {}
        }
        
        for class_name, results in by_class.items():
            times = [r.execution_time for r in results]
            mflops = [r.mflops for r in results]
            
            summary['by_class'][class_name] = {
                'count': len(results),
                'avg_time': sum(times) / len(times),
                'min_time': min(times),
                'max_time': max(times),
                'avg_mflops': sum(mflops) / len(mflops),
                'min_mflops': min(mflops),
                'max_mflops': max(mflops)
            }
        
        return summary
    
    def _generate_markdown_report(self, data: Dict):
        md_content = f"""# NPB-Rust Comprehensive Analysis Report

Generated on: {data['metadata']['timestamp']}

## Test Configuration
- **Classes tested**: {data['metadata']['test_classes']}
- **Max threads**: {data['metadata']['max_threads']}
- **Iterations**: {data['metadata']['iterations']}
- **Binary**: {data['metadata']['binary_path']}

## System Information
- **Host**: {data['system_info']['hostname']}
- **OS**: {data['system_info']['os']} {data['system_info']['arch']} (Kernel: {data['system_info']['kernel']})
- **CPU**: {data['system_info']['cpu_model']}
- **Cores**: {data['system_info']['total_cores']}
- **Memory**: {data['system_info']['memory_gb']:.1f} GB

## Hardware Topology
"""
        
        if 'cores' in data['hardware_topology']:
            md_content += f"- **CPU Cores**: {data['hardware_topology']['cores']}\n"
        if 'threads' in data['hardware_topology']:
            md_content += f"- **Hardware Threads**: {data['hardware_topology']['threads']}\n"
        
        md_content += f"""
## Code Quality Metrics

| Metric | Value |
|--------|-------|
| Rust Lines of Code | {data['code_quality']['rust_loc']:,} |
| Clippy Warnings | {data['code_quality']['clippy_warnings']} |
| Clippy Errors | {data['code_quality']['clippy_errors']} |
| Binary Size | {data['code_quality']['binary_size_mb']:.2f} MB |

## Benchmark Results Summary

"""
        
        if 'benchmark_summary' in data and data['benchmark_summary']:
            summary = data['benchmark_summary']
            md_content += f"""### Overall Results
- **Total runs**: {summary['total_runs']}
- **Successful**: {summary['successful_runs']}
- **Failed**: {summary['failed_runs']}

### Results by Class
"""
            
            for class_name, class_data in summary['by_class'].items():
                md_content += f"""#### Class {class_name}
- **Test count**: {class_data['count']}
- **Average execution time**: {class_data['avg_time']:.3f}s
- **Time range**: {class_data['min_time']:.3f}s - {class_data['max_time']:.3f}s
- **Average MFLOPS**: {class_data['avg_mflops']:.2f}
- **MFLOPS range**: {class_data['min_mflops']:.2f} - {class_data['max_mflops']:.2f}

"""
        
        if data['thread_affinity_summary']:
            md_content += """## Thread Affinity Analysis

"""
            for class_name, affinity_data in data['thread_affinity_summary'].items():
                md_content += f"""### Class {class_name}
"""
                for strategy, time_val in affinity_data.items():
                    md_content += f"- **{strategy}**: {time_val:.3f}s\n"
                md_content += "\n"
        
        if data['performance_profiling_summary']:
            md_content += """## Performance Profiling Summary

Performance profiling data has been collected and saved to CSV files for detailed analysis.

"""
        
        md_content += """## Data Files Generated

The following data files have been generated for further analysis:

- `benchmark_results.json` - Complete benchmark results in JSON format
- `benchmark_results.csv` - Benchmark results in CSV format for easy analysis
- `comprehensive_analysis.json` - Complete analysis summary
- `thread_affinity_results.csv` - Thread affinity test results (if available)
- `performance_profiling.csv` - Performance profiling data (if available)
- `topology.svg` - Hardware topology visualization (if hwloc available)

## Analysis Recommendations

### Python Analysis Example
```python
import pandas as pd
import matplotlib.pyplot as plt

# Load benchmark data
df = pd.read_csv('benchmark_results.csv')

# Group by class and threads to analyze scaling
scaling = df.groupby(['class', 'threads']).agg({
    'execution_time_seconds': ['mean', 'std'],
    'mflops': ['mean', 'std']
}).round(3)

print(scaling)

# Plot scaling performance
for class_name in df['class'].unique():
    class_data = df[df['class'] == class_name]
    avg_by_threads = class_data.groupby('threads')['mflops'].mean()
    plt.plot(avg_by_threads.index, avg_by_threads.values, marker='o', label=f'Class {class_name}')

plt.xlabel('Thread Count')
plt.ylabel('MFLOPS')
plt.title('Performance Scaling by Thread Count')
plt.legend()
plt.grid(True)
plt.show()
```
"""
        
        with open(self.output_dir / 'analysis_report.md', 'w') as f:
            f.write(md_content)
    
    def save_system_info(self, system_info: SystemInfo, test_config: Dict):
        system_data = {
            "benchmark_info": {
                "binary_path": str(self.binary_path),
                "classes_tested": test_config['classes'],
                "max_threads": test_config['max_threads'],
                "iterations_per_config": test_config['iterations'],
                "total_runs": len(self.benchmark_results),
                "timestamp": time.strftime('%Y-%m-%dT%H:%M:%S.%fZ')
            },
            "system_info": {
                "hostname": system_info.hostname,
                "os": system_info.os,
                "arch": system_info.arch,
                "kernel": system_info.kernel,
                "cpu_model": system_info.cpu_model,
                "total_cores": system_info.total_cores,
                "memory_gb": system_info.memory_gb
            }
        }
        
        with open(self.output_dir / 'system_info.json', 'w') as f:
            json.dump(system_data, f, indent=2)
        
        print(f"  ✓ System info saved to {self.output_dir}/system_info.json")

def main():
    if len(sys.argv) < 4:
        print("Usage: python3 unified_npb_analyzer.py BINARY_PATH TEST_CLASSES MAX_THREADS [ITERATIONS] [OUTPUT_DIR]")
        print("Example: python3 unified_npb_analyzer.py ./target/release/is ABC 8 5")
        print("")
        print("This unified tool performs:")
        print("  1. Basic benchmark runs with detailed timing")
        print("  2. Code quality analysis (Clippy, LOC, binary size)")
        print("  3. Hardware topology analysis (if hwloc available)")
        print("  4. Thread affinity testing (if hwloc-bind available)")
        print("  5. Performance profiling (perf on Linux, instruments on macOS)")
        print("  6. Comprehensive reporting in JSON, CSV, and Markdown formats")
        sys.exit(1)
    
    binary_path = sys.argv[1]
    test_classes = sys.argv[2]
    max_threads = int(sys.argv[3])
    iterations = int(sys.argv[4]) if len(sys.argv) > 4 else 5
    output_dir = sys.argv[5] if len(sys.argv) > 5 else 'benchmark_results'
    
    if not Path(binary_path).exists():
        print(f"Error: Binary not found at {binary_path}")
        print("Please provide the correct path to your binary as first argument")
        sys.exit(1)
    
    print("🔬 NPB-Rust Unified Benchmark & Analysis Tool")
    print("=" * 60)
    print(f"Binary: {binary_path}")
    print(f"Test classes: {test_classes}")
    print(f"Max threads: {max_threads}")
    print(f"Iterations: {iterations}")
    print(f"Output: {output_dir}")
    print("=" * 60)
    
    analyzer = UnifiedNPBAnalyzer(binary_path, output_dir)
    
    print("\n📊 PHASE 1: System Analysis")
    print("-" * 30)
    system_info = analyzer.get_system_info()
    print(f"System: {system_info.os} {system_info.arch} - {system_info.cpu_model}")
    
    topology = analyzer.get_hwloc_topology()
    
    print("\n🚀 PHASE 2: Basic Benchmarking")
    print("-" * 30)
    analyzer.run_basic_benchmarks(test_classes, max_threads, iterations)
    analyzer.save_benchmark_results()
    analyzer.generate_summary_stats()
    
    test_config = {
        'classes': test_classes,
        'max_threads': max_threads,
        'iterations': iterations
    }
    analyzer.save_system_info(system_info, test_config)
    
    print("\n🔍 PHASE 3: Quality Analysis")
    print("-" * 30)
    quality_metrics = analyzer.analyze_code_quality()
    
    print("\n🧵 PHASE 4: Thread Affinity Testing")
    print("-" * 30)
    affinity_results = analyzer.test_thread_affinity(test_classes, max_threads, min(3, iterations))
    
    print("\n🚀 PHASE 5: Performance Profiling")
    print("-" * 30)
    profiling_results = analyzer.run_performance_profiling(test_classes, max_threads)
    
    print("\n📝 PHASE 6: Report Generation")
    print("-" * 30)
    analyzer.generate_comprehensive_report(
        system_info, topology, quality_metrics, 
        profiling_results, affinity_results, test_config
    )
    
    print("\n" + "=" * 60)
    print("✅ UNIFIED ANALYSIS COMPLETE!")
    print(f"📁 All results saved to: {output_dir}/")
    print("\n📊 Key Files Generated:")
    print(f"  📈 benchmark_results.csv - Raw benchmark data")
    print(f"  📋 analysis_report.md - Human-readable report")
    print(f"  🔧 comprehensive_analysis.json - Complete analysis data")
    
    topology_svg = Path(output_dir) / "topology.svg"
    if topology_svg.exists():
        print(f"  🗺️ topology.svg - Hardware visualization")
    
    affinity_csv = Path(output_dir) / "thread_affinity_results.csv"
    if affinity_csv.exists():
        print(f"  🧵 thread_affinity_results.csv - Affinity analysis")
    
    perf_csv = Path(output_dir) / "performance_profiling.csv"
    if perf_csv.exists():
        print(f"  🚀 performance_profiling.csv - Performance counters")
    
    if affinity_results:
        print("\n🎯 OPTIMIZATION RECOMMENDATIONS:")
        for class_char, results in affinity_results.items():
            if results:
                best_config = min(results.keys(), key=lambda k: results[k])
                best_time = results[best_config]
                worst_time = max(results.values())
                improvement = ((worst_time / best_time - 1) * 100) if best_time > 0 else 0
                print(f"   Class {class_char}: Use '{best_config}' binding (up to {improvement:.1f}% faster)")
    
    print("\n📈 ANALYSIS COMMANDS:")
    print("  # Basic analysis with pandas:")
    print(f"  import pandas as pd")
    print(f"  df = pd.read_csv('{output_dir}/benchmark_results.csv')")
    print(f"  print(df.groupby(['class', 'threads']).agg({{'execution_time_seconds': ['mean', 'std'], 'mflops': ['mean', 'std']}}))")
    print("")
    print("  # View the markdown report:")
    print(f"  cat {output_dir}/analysis_report.md")

if __name__ == '__main__':
    main()