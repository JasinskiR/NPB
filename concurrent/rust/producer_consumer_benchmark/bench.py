#!/usr/bin/env python3

import subprocess
import time
import os
import csv
import sys
import platform
import psutil
import threading
from pathlib import Path
from datetime import datetime
import signal
import socket
import re

class BenchmarkRunner:
    def __init__(self):
        self.results = []
        self.system_info = self.collect_system_info()
        self.output_dir = Path("benchmark_results")
        self.output_dir.mkdir(exist_ok=True)
        
    def collect_system_info(self):
        return {
            'os': platform.system(),
            'architecture': platform.machine(),
            'cores': psutil.cpu_count(logical=False),
            'logical_cores': psutil.cpu_count(logical=True),
            'total_memory': psutil.virtual_memory().total,
            'python_version': platform.python_version()
        }
    
    def build_programs(self):
        print("Building programs...")
        
        cpp_build_commands = [
            ["g++", "-std=c++20", "-O3", "-pthread", "-o", "cpp_concurrent", "main.cpp"],
            ["g++", "-std=c++20", "-O3", "-pthread", "-o", "cpp_async", "async_main.cpp"]
        ]
        
        rust_build_commands = [
            ["cargo", "build", "--release", "--bin", "concurrent"],
            ["cargo", "build", "--release", "--bin", "async_tokio"]
        ]
        
        for cmd in cpp_build_commands:
            try:
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
                if result.returncode != 0:
                    print(f"Failed to build C++: {result.stderr}")
                    return False
            except subprocess.TimeoutExpired:
                print("C++ build timeout")
                return False
        
        for cmd in rust_build_commands:
            try:
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
                if result.returncode != 0:
                    print(f"Failed to build Rust: {result.stderr}")
                    return False
            except subprocess.TimeoutExpired:
                print("Rust build timeout")
                return False
        
        print("Build completed successfully")
        return True
    
    def get_binary_size(self, binary_path):
        try:
            return os.path.getsize(binary_path)
        except:
            return 0
    
    def measure_compilation_time(self, build_command):
        start_time = time.time()
        try:
            result = subprocess.run(build_command, capture_output=True, text=True, timeout=120)
            compilation_time = time.time() - start_time
            return compilation_time, result.returncode == 0
        except subprocess.TimeoutExpired:
            return time.time() - start_time, False
    
    def monitor_process_resources(self, process, metrics):
        try:
            while process.poll() is None:
                try:
                    proc = psutil.Process(process.pid)
                    memory_info = proc.memory_info()
                    cpu_percent = proc.cpu_percent()
                    
                    metrics['memory_samples'].append(memory_info.rss)
                    metrics['cpu_samples'].append(cpu_percent)
                    
                    if memory_info.rss > metrics['peak_memory']:
                        metrics['peak_memory'] = memory_info.rss
                    
                    for child in proc.children(recursive=True):
                        child_memory = child.memory_info()
                        metrics['memory_samples'].append(child_memory.rss)
                        if child_memory.rss > metrics['peak_memory']:
                            metrics['peak_memory'] = child_memory.rss
                
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    break
                
                time.sleep(0.1)
        except Exception:
            pass
    
    def run_concurrent_benchmark(self, program_path, language, threads, items, run_number):
        print(f"Running {language} concurrent benchmark: {threads} threads, run {run_number}")
        
        start_time = time.time()
        metrics = {
            'memory_samples': [],
            'cpu_samples': [],
            'peak_memory': 0
        }
        
        try:
            if language == "rust":
                cmd = ["./target/release/concurrent", str(threads), str(items)]
            else:
                cmd = [program_path, str(threads), str(items)]
            
            process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            
            monitor_thread = threading.Thread(target=self.monitor_process_resources, args=(process, metrics))
            monitor_thread.start()
            
            stdout, stderr = process.communicate(timeout=300)
            monitor_thread.join(timeout=1)
            
            execution_time = time.time() - start_time
            
            parsed_metrics = self.parse_concurrent_output(stdout)
            
            result = {
                'timestamp': datetime.now().isoformat(),
                'language': language,
                'test_type': 'concurrent',
                'threads': threads,
                'items': items,
                'run_number': run_number,
                'execution_time': execution_time,
                'binary_size': self.get_binary_size(program_path),
                'peak_memory_usage': metrics['peak_memory'],
                'avg_memory_usage': sum(metrics['memory_samples']) / len(metrics['memory_samples']) if metrics['memory_samples'] else 0,
                'avg_cpu_usage': sum(metrics['cpu_samples']) / len(metrics['cpu_samples']) if metrics['cpu_samples'] else 0,
                'success': process.returncode == 0
            }
            
            result.update(parsed_metrics)
            
            return result
            
        except subprocess.TimeoutExpired:
            process.kill()
            return {
                'timestamp': datetime.now().isoformat(),
                'language': language,
                'test_type': 'concurrent',
                'threads': threads,
                'items': items,
                'run_number': run_number,
                'execution_time': 300,
                'success': False,
                'error': 'timeout'
            }
        except Exception as e:
            return {
                'timestamp': datetime.now().isoformat(),
                'language': language,
                'test_type': 'concurrent',
                'threads': threads,
                'items': items,
                'run_number': run_number,
                'success': False,
                'error': str(e)
            }
    
    def run_async_benchmark(self, program_path, language, run_number):
        print(f"Running {language} async benchmark: run {run_number}")
        
        start_time = time.time()
        metrics = {
            'memory_samples': [],
            'cpu_samples': [],
            'peak_memory': 0
        }
        
        try:
            if language == "rust":
                cmd = ["./target/release/async_tokio"]
            else:
                cmd = [program_path]
            
            process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            
            monitor_thread = threading.Thread(target=self.monitor_process_resources, args=(process, metrics))
            monitor_thread.start()
            
            stdout, stderr = process.communicate(timeout=300)
            monitor_thread.join(timeout=1)
            
            execution_time = time.time() - start_time
            
            parsed_metrics = self.parse_async_output(stdout)
            
            result = {
                'timestamp': datetime.now().isoformat(),
                'language': language,
                'test_type': 'async',
                'run_number': run_number,
                'execution_time': execution_time,
                'binary_size': self.get_binary_size(program_path),
                'peak_memory_usage': metrics['peak_memory'],
                'avg_memory_usage': sum(metrics['memory_samples']) / len(metrics['memory_samples']) if metrics['memory_samples'] else 0,
                'avg_cpu_usage': sum(metrics['cpu_samples']) / len(metrics['cpu_samples']) if metrics['cpu_samples'] else 0,
                'success': process.returncode == 0
            }
            
            result.update(parsed_metrics)
            
            return result
            
        except subprocess.TimeoutExpired:
            process.kill()
            return {
                'timestamp': datetime.now().isoformat(),
                'language': language,
                'test_type': 'async',
                'run_number': run_number,
                'execution_time': 300,
                'success': False,
                'error': 'timeout'
            }
        except Exception as e:
            return {
                'timestamp': datetime.now().isoformat(),
                'language': language,
                'test_type': 'async',
                'run_number': run_number,
                'success': False,
                'error': str(e)
            }
    
    def parse_concurrent_output(self, output):
        metrics = {}
        
        patterns = {
            'mutex_operations_per_second': r'(\d+(?:\.\d+)?)\s+ops/s',
            'mutex_avg_lock_time': r'Avg lock time:\s+(\d+(?:\.\d+)?)\s+μs',
            'channel_operations_per_second': r'(\d+(?:\.\d+)?)\s+ops/s',
            'channel_avg_latency': r'Avg latency:\s+(\d+(?:\.\d+)?)\s+μs',
            'context_switches': r'Context switches:\s+(\d+)',
            'produced_total': r'Produced:\s+(\d+)',
            'consumed_total': r'Consumed:\s+(\d+)',
            'efficiency_percent': r'Efficiency:\s+(\d+(?:\.\d+)?)%',
            'threads_created': r'Threads created:\s+(\d+)',
            'avg_thread_creation_time': r'Average creation time:\s+(\d+(?:\.\d+)?)\s+μs'
        }
        
        for key, pattern in patterns.items():
            matches = re.findall(pattern, output)
            if matches:
                try:
                    metrics[key] = float(matches[-1])
                except ValueError:
                    metrics[key] = 0
            else:
                metrics[key] = 0
        
        return metrics
    
    def parse_async_output(self, output):
        metrics = {}
        
        patterns = {
            'total_tasks_spawned': r'Total tasks spawned:\s+(\d+)',
            'avg_spawn_time': r'Avg spawn time:\s+(\d+(?:\.\d+)?)\s+μs',
            'tasks_per_second': r'Tasks per second:\s+(\d+(?:\.\d+)?)',
            'total_async_operations': r'Total operations:\s+(\d+)',
            'avg_operation_time': r'Avg operation time:\s+(\d+(?:\.\d+)?)\s+μs',
            'operations_per_second': r'Operations per second:\s+(\d+(?:\.\d+)?)',
            'total_connections': r'Total:\s+(\d+)',
            'peak_concurrent_connections': r'Peak concurrent:\s+(\d+)',
            'connection_rate': r'Rate:\s+(\d+(?:\.\d+)?)\s+conn/s',
            'total_messages': r'Messages:\s+(\d+)',
            'messages_per_second': r'Messages/s:\s+(\d+(?:\.\d+)?)',
            'total_bytes': r'Bytes:\s+(\d+)',
            'throughput_mbps': r'Throughput:\s+(\d+(?:\.\d+)?)\s+MB/s'
        }
        
        for key, pattern in patterns.items():
            matches = re.findall(pattern, output)
            if matches:
                try:
                    metrics[key] = float(matches[-1])
                except ValueError:
                    metrics[key] = 0
            else:
                metrics[key] = 0
        
        return metrics
    
    def run_benchmark_suite(self):
        if not self.build_programs():
            print("Build failed, exiting")
            return
        
        programs = [
            ("cpp_concurrent", "cpp"),
            ("./target/release/concurrent", "rust")
        ]
        
        async_programs = [
            ("cpp_async", "cpp"),
            ("./target/release/async_tokio", "rust")
        ]
        
        items_per_test = 1000
        
        for threads in range(1, 9):
            for run in range(1, 11):
                for program_path, language in programs:
                    result = self.run_concurrent_benchmark(program_path, language, threads, items_per_test, run)
                    self.results.append(result)
                    time.sleep(1)
        
        for run in range(1, 11):
            for program_path, language in async_programs:
                result = self.run_async_benchmark(program_path, language, run)
                self.results.append(result)
                time.sleep(1)
    
    def save_results(self):
        concurrent_results = [r for r in self.results if r.get('test_type') == 'concurrent']
        async_results = [r for r in self.results if r.get('test_type') == 'async']
        
        if concurrent_results:
            concurrent_file = self.output_dir / f"concurrent_benchmark_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
            with open(concurrent_file, 'w', newline='') as f:
                if concurrent_results:
                    writer = csv.DictWriter(f, fieldnames=concurrent_results[0].keys())
                    writer.writeheader()
                    writer.writerows(concurrent_results)
            print(f"Concurrent results saved to {concurrent_file}")
        
        if async_results:
            async_file = self.output_dir / f"async_benchmark_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
            with open(async_file, 'w', newline='') as f:
                if async_results:
                    writer = csv.DictWriter(f, fieldnames=async_results[0].keys())
                    writer.writeheader()
                    writer.writerows(async_results)
            print(f"Async results saved to {async_file}")
        
        system_file = self.output_dir / f"system_info_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        with open(system_file, 'w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=self.system_info.keys())
            writer.writeheader()
            writer.writerow(self.system_info)
        print(f"System info saved to {system_file}")

def main():
    if len(sys.argv) > 1 and sys.argv[1] in ['-h', '--help']:
        print("Usage: python benchmark_runner.py")
        print("Runs comprehensive benchmarks for C++ and Rust concurrent/async programs")
        print("Results will be saved to benchmark_results/ directory")
        return
    
    runner = BenchmarkRunner()
    
    try:
        print("Starting comprehensive benchmark suite...")
        print(f"System: {runner.system_info['os']} {runner.system_info['architecture']}")
        print(f"Cores: {runner.system_info['cores']} physical, {runner.system_info['logical_cores']} logical")
        print(f"Memory: {runner.system_info['total_memory'] // (1024**3)} GB")
        
        runner.run_benchmark_suite()
        runner.save_results()
        
        print("Benchmark suite completed successfully")
        
    except KeyboardInterrupt:
        print("\nBenchmark interrupted by user")
        runner.save_results()
    except Exception as e:
        print(f"Error during benchmark: {e}")
        runner.save_results()

if __name__ == "__main__":
    main()