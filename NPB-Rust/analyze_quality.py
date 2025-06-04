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

@dataclass
class BenchmarkResult:
    class_name: str
    threads: int
    execution_time: float
    mflops: float
    verification_passed: bool

@dataclass
class SystemInfo:
    hostname: str
    os: str
    arch: str
    cpu_model: str
    total_cores: int
    memory_gb: float

@dataclass
class QualityMetrics:
    loc_rust: int = 0
    clippy_warnings: int = 0
    clippy_errors: int = 0
    binary_size_mb: float = 0.0

class SimpleQualityAnalyzer:
    def __init__(self, binary_path: str, output_dir: str):
        self.binary_path = Path(binary_path)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        self.os_type = platform.system().lower()
        self.core_distribution_data = []
        self.perf_data = []
        
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
            cpu_model=cpu_model,
            total_cores=total_cores,
            memory_gb=memory_gb
        )
    
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
            
            cmd = [
                'perf', 'stat',
                '-e', 'page-faults,minor-faults,major-faults',
                str(self.binary_path), class_char, str(max_threads)
            ]
            
            exit_code, stdout, stderr = self.run_command(cmd)
            if exit_code == 0:
                memory_data = self._parse_perf_memory(stderr)
                class_results['memory_counters'] = memory_data
                
                for metric, value in memory_data.items():
                    if isinstance(value, (int, float)):
                        self.perf_data.append({
                            'class': class_char,
                            'metric': metric,
                            'value': value,
                            'unit': 'count'
                        })
            
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
    
    def _parse_perf_memory(self, perf_output: str) -> Dict:
        data = {}
        
        for line in perf_output.split('\n'):
            line = line.strip()
            if 'page-faults' in line:
                try:
                    data['page_faults'] = int(line.split()[0].replace(',', ''))
                except:
                    pass
            elif 'minor-faults' in line:
                try:
                    data['minor_faults'] = int(line.split()[0].replace(',', ''))
                except:
                    pass
            elif 'major-faults' in line:
                try:
                    data['major_faults'] = int(line.split()[0].replace(',', ''))
                except:
                    pass
        
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
    
    def run_performance_benchmark(self, test_classes: str, max_threads: int, iterations: int) -> List[BenchmarkResult]:
        results = []
        
        print("🚀 Running performance benchmarks...")
        
        monitoring_cmd = 'hwloc-ps'
        if self.os_type == 'darwin':
            monitoring_available = self.run_command(['which', 'ps'])[0] == 0
        else:
            monitoring_available = self.run_command(['which', 'hwloc-ps'])[0] == 0
    
        if not monitoring_available:
            print("  ⚠️ Process-core mapping tools not available - cannot monitor core distribution")
        
        for class_char in test_classes:
            print(f"  Testing class {class_char}...")
            
            for num_threads_to_test in range(1, max_threads + 1):
                print(f"    Running with {num_threads_to_test} thread(s)...")
                for iteration in range(iterations):
                    start_time = time.time()
                    process = subprocess.Popen(
                        [str(self.binary_path), class_char, str(num_threads_to_test)],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        text=True
                    )
                    
                    core_distribution_samples = []
                    if monitoring_available:
                        # print(f"    📊 Monitoring core distribution...") # Less verbose
                        
                        for sample_num in range(100): # Consider adjusting sample count or duration
                            if process.poll() is not None:
                                break
                            
                            if self.os_type == 'darwin':
                                exit_code, stdout, _ = self.run_command(['ps', '-M', '-p', str(process.pid)])
                                if exit_code == 0 and stdout:
                                    timestamp = time.time() - start_time
                                    self._parse_macos_core_distribution(stdout, class_char, iteration, sample_num, timestamp, num_threads_to_test)
                            else:
                                exit_code, stdout, _ = self.run_command(['hwloc-ps', '-a', '--pid', str(process.pid)])
                                if exit_code == 0 and stdout:
                                    timestamp = time.time() - start_time
                                    self._parse_linux_core_distribution(stdout, class_char, iteration, sample_num, timestamp, num_threads_to_test)
                            
                            time.sleep(0.1) # Sampling interval
                    
                    stdout, stderr = process.communicate()
                    end_time = time.time()
                    exit_code = process.returncode
                    
                    if exit_code == 0:
                        execution_time = end_time - start_time
                        
                        verification_passed = "VERIFICATION SUCCESSFUL" in stdout or "Verification    =               SUCCESSFUL" in stdout
                        mflops = 0.0
                        
                        for line in stdout.split('\n'):
                            if 'Mop/s total' in line:
                                try:
                                    mflops = float(line.split('=')[1].strip())
                                except:
                                    pass
                        
                        result = BenchmarkResult(
                            class_name=class_char,
                            threads=num_threads_to_test,
                            execution_time=execution_time,
                            mflops=mflops,
                            verification_passed=verification_passed
                        )
                        results.append(result)
                        
                        print(f"      Iteration {iteration + 1} ({num_threads_to_test} threads): {execution_time:.3f}s, {mflops:.2f} MFLOPS, Verified: {verification_passed}")
                    else:
                        print(f"      Iteration {iteration + 1} ({num_threads_to_test} threads) FAILED. Exit code: {exit_code}")
                        print(f"        Stdout: {stdout}")
                        print(f"        Stderr: {stderr}")
        
        if self.core_distribution_data:
            csv_path = self.output_dir / 'core_distribution.csv'
            with open(csv_path, 'w', newline='') as f:
                fieldnames = ['class', 'iteration', 'sample', 'timestamp', 'thread_id', 'core_id', 'cpu_usage', 'num_threads_tested']
                writer = csv.DictWriter(f, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerows(self.core_distribution_data)
            print(f"  ✓ Core distribution data saved to {csv_path}")
        
        return results
    
    def _parse_macos_core_distribution(self, ps_output: str, class_char: str, iteration: int, sample_num: int, timestamp: float, num_threads_tested: int):
        lines = ps_output.strip().split('\n')
        if len(lines) > 1:
            for line in lines[1:]:
                parts = line.split()
                if len(parts) >= 4:
                    try:
                        tid = parts[0]
                        cpu_id = int(parts[1])
                        cpu_usage = parts[2] if len(parts) > 2 else '0'
                        
                        self.core_distribution_data.append({
                            'class': class_char,
                            'iteration': iteration + 1,
                            'sample': sample_num + 1,
                            'timestamp': round(timestamp, 3),
                            'thread_id': tid,
                            'core_id': cpu_id,
                            'cpu_usage': cpu_usage,
                            'num_threads_tested': num_threads_tested
                        })
                    except:
                        pass
    
    def _parse_linux_core_distribution(self, hwloc_output: str, class_char: str, iteration: int, sample_num: int, timestamp: float, num_threads_tested: int):
        for line in hwloc_output.strip().split('\n'):
            if line and 'PU' in line:
                try:
                    parts = line.split()
                    if len(parts) >= 3:
                        tid = parts[0]
                        core_info = [part for part in parts if 'PU' in part]
                        if core_info:
                            core_id = core_info[0].replace('PU:', '').replace('PU', '')
                            
                            self.core_distribution_data.append({
                                'class': class_char,
                                'iteration': iteration + 1,
                                'sample': sample_num + 1,
                                'timestamp': round(timestamp, 3),
                                'thread_id': tid,
                                'core_id': core_id,
                                'cpu_usage': '0', # hwloc-ps doesn't directly give CPU usage per thread easily
                                'num_threads_tested': num_threads_tested
                            })
                except:
                    pass
    
    def generate_report(self, system_info: SystemInfo, topology: Dict, 
                       quality_metrics: QualityMetrics, benchmark_results: List[BenchmarkResult],
                       profiling_results: Dict, affinity_results: Dict, test_config: Dict):
        print("📝 Generating analysis report...")
        
        benchmark_csv_data = []
        for result in benchmark_results:
            benchmark_csv_data.append({
                'class': result.class_name,
                'threads': result.threads,
                'execution_time': result.execution_time,
                'mflops': result.mflops,
                'verification_passed': result.verification_passed
            })
        
        if benchmark_csv_data:
            csv_path = self.output_dir / 'benchmark_results.csv'
            with open(csv_path, 'w', newline='') as f:
                fieldnames = ['class', 'threads', 'execution_time', 'mflops', 'verification_passed']
                writer = csv.DictWriter(f, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerows(benchmark_csv_data)
            print(f"  ✓ Benchmark results saved to {csv_path}")
        
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
            'performance_profiling_summary': profiling_results
        }
        
        with open(self.output_dir / 'analysis_summary.json', 'w') as f:
            json.dump(report_data, f, indent=2)
        
        self._generate_markdown_report(report_data, benchmark_results)
        
        print(f"    ✓ Summary report saved to {self.output_dir}/analysis_summary.json")
        print(f"    ✓ Markdown report saved to {self.output_dir}/analysis_report.md")
    
    def _generate_markdown_report(self, data: Dict, benchmark_results: List[BenchmarkResult]):
        md_content = f"""# NPB-Rust Quality Analysis Report

Generated on: {data['metadata']['timestamp']}

## Test Configuration
- **Classes tested**: {data['metadata']['test_classes']}
- **Max threads**: {data['metadata']['max_threads']}
- **Iterations**: {data['metadata']['iterations']}
- **Binary**: {data['metadata']['binary_path']}

## System Information
- **Host**: {data['system_info']['hostname']}
- **OS**: {data['system_info']['os']} {data['system_info']['arch']}
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
        
        by_class = {}
        for result in benchmark_results:
            class_name = result.class_name
            if class_name not in by_class:
                by_class[class_name] = []
            by_class[class_name].append(result)
        
        for class_name, results in by_class.items():
            times = [r.execution_time for r in results]
            mflops = [r.mflops for r in results]
            
            avg_time = sum(times) / len(times)
            avg_mflops = sum(mflops) / len(mflops)
            
            md_content += f"""### Class {class_name}
- **Average execution time**: {avg_time:.3f}s
- **Average MFLOPS**: {avg_mflops:.2f}
- **All tests passed**: {all(r.verification_passed for r in results)}

"""

def main():
    if len(sys.argv) < 4:
        print("Usage: python3 analyze_quality.py BINARY_PATH TEST_CLASSES MAX_THREADS [ITERATIONS] [OUTPUT_DIR]")
        print("Example: python3 analyze_quality.py ./target/release/cg-pp-s SWA 8 3")
        sys.exit(1)
    
    binary_path = sys.argv[1]
    test_classes = sys.argv[2]
    max_threads = int(sys.argv[3])
    iterations = int(sys.argv[4]) if len(sys.argv) > 4 else 3
    output_dir = sys.argv[5] if len(sys.argv) > 5 else 'quality_analysis'
    
    print("🔬 NPB-Rust Quality & Performance Analyzer")
    print("=" * 50)
    print(f"Binary: {binary_path}")
    print(f"Test classes: {test_classes}")
    print(f"Max threads: {max_threads}")
    print(f"Iterations: {iterations}")
    print(f"Output: {output_dir}")
    print("=" * 50)
    
    analyzer = SimpleQualityAnalyzer(binary_path, output_dir)
    
    # Get system information
    system_info = analyzer.get_system_info()
    print(f"System: {system_info.os} {system_info.arch} - {system_info.cpu_model}")
    
    # Analyze hardware topology
    topology = analyzer.get_hwloc_topology()
    
    # Analyze code quality
    quality_metrics = analyzer.analyze_code_quality()
    
    # Run performance benchmarks
    benchmark_results = analyzer.run_performance_benchmark(test_classes, max_threads, iterations)
    
    # Run detailed performance profiling
    profiling_results = analyzer.run_performance_profiling(test_classes, max_threads)
    
    # Test thread affinity
    affinity_results = analyzer.test_thread_affinity(test_classes, max_threads, min(3, iterations))
    
    # Generate report
    test_config = {
        'classes': test_classes,
        'max_threads': max_threads,
        'iterations': iterations
    }
    
    analyzer.generate_report(
        system_info, topology, quality_metrics, 
        benchmark_results, profiling_results, affinity_results, test_config
    )
    
    print("\n" + "=" * 50)
    print("✅ Analysis complete!")
    print(f"📁 Results saved to: {output_dir}/")
    print(f"📊 View report: {output_dir}/quality_analysis_report.md")
    
    # Show topology visualization info
    topology_svg = Path(output_dir) / "topology.svg"
    if topology_svg.exists():
        print(f"🗺️ Hardware topology: {topology_svg} (open in browser)")
    
    # Show core distribution info
    if hasattr(analyzer, 'core_distribution_files') and analyzer.core_distribution_files:
        print("\n📊 Core Distribution Analysis:")
        for file in analyzer.core_distribution_files:
            print(f"   - {file}")
    
    # Show optimization tips
    if affinity_results:
        print("\n🎯 Optimization Tips:")
        for class_char, results in affinity_results.items():
            if results:
                best_config = min(results.keys(), key=lambda k: results[k])
                best_time = results[best_config]
                worst_time = max(results.values())
                improvement = ((worst_time / best_time - 1) * 100) if best_time > 0 else 0
                print(f"   Class {class_char}: Use '{best_config}' binding (up to {improvement:.1f}% faster)")
                
if __name__ == '__main__':
    main()