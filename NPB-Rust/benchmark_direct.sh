#!/bin/bash

set -e

echo "=== NPB-Rust Direct Binary Benchmark ==="

BINARY_PATH=${1:-"./target/release/is"}
CLASSES=${2:-"ABC"}
MAX_THREADS=${3:-8}
ITERATIONS=${4:-5}
OUTPUT_DIR=${5:-"benchmark_results"}

echo "Binary path: $BINARY_PATH"
echo "Classes to test: $CLASSES"
echo "Max threads: $MAX_THREADS"
echo "Iterations per config: $ITERATIONS"
echo "Output directory: $OUTPUT_DIR"
echo ""

if [ ! -f "$BINARY_PATH" ]; then
    echo "Error: Binary not found at $BINARY_PATH"
    echo "Please provide the correct path to your binary as first argument"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

get_system_info() {
    echo "{"
    echo "  \"hostname\": \"$(hostname)\","
    echo "  \"os\": \"$(uname -s)\","
    echo "  \"arch\": \"$(uname -m)\","
    echo "  \"kernel\": \"$(uname -r)\","
    
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        CPU_MODEL=$(grep "model name" /proc/cpuinfo | head -1 | cut -d: -f2 | sed 's/^ *//' | sed 's/ *$//')
        echo "  \"cpu_model\": \"$CPU_MODEL\","
        TOTAL_CORES=$(nproc)
        echo "  \"total_cores\": $TOTAL_CORES,"
        MEMORY_KB=$(grep MemTotal /proc/meminfo | awk '{print $2}')
        MEMORY_GB=$(echo "scale=2; $MEMORY_KB / 1024 / 1024" | bc -l)
        echo "  \"memory_gb\": $MEMORY_GB"
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        CPU_MODEL=$(sysctl -n machdep.cpu.brand_string)
        echo "  \"cpu_model\": \"$CPU_MODEL\","
        TOTAL_CORES=$(sysctl -n hw.ncpu)
        echo "  \"total_cores\": $TOTAL_CORES,"
        MEMORY_BYTES=$(sysctl -n hw.memsize)
        MEMORY_GB=$(echo "scale=2; $MEMORY_BYTES / 1024 / 1024 / 1024" | bc -l)
        echo "  \"memory_gb\": $MEMORY_GB"
    else
        echo "  \"cpu_model\": \"Unknown\","
        echo "  \"total_cores\": 0,"
        echo "  \"memory_gb\": 0"
    fi
    echo "}"
}

get_memory_usage() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        ps -o pid,vsz,rss,comm -p $$ | tail -1 | awk '{print $3}'
    else
        echo "0"
    fi
}

parse_is_output() {
    local output="$1"
    local verification="false"
    local mops="0.0" # IS uses Mop/s (Mega Operations per second)
    local time="0.0"
    local class_npb_reported="N/A"
    local size_reported="N/A"
    local total_threads_reported="0"
    local iterations_reported="0"
    local operation_type="N/A"
    local version_npb="N/A"
    
    if echo "$output" | grep -q "Verification    =               SUCCESSFUL"; then
        verification="true"
    fi
    
    # IS benchmark specific parsing
    mops=$(echo "$output" | grep "Mop/s total" | awk -F'=' '{print $2}' | tr -d ' ' || echo "0.0")
    time=$(echo "$output" | grep "Time in seconds" | awk -F'=' '{print $2}' | tr -d ' ' || echo "0.0")
    class_npb_reported=$(echo "$output" | grep "class_npb       =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "N/A")
    size_reported=$(echo "$output" | grep "Size            =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "N/A")
    total_threads_reported=$(echo "$output" | grep "Total threads   =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "0")
    iterations_reported=$(echo "$output" | grep "Iterations      =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "0")
    operation_type=$(echo "$output" | grep "Operation type  =" | awk -F'=' '{print $2}' | sed 's/^ *//' | sed 's/ *$//' || echo "N/A") # Adjusted for "keys ranked"
    version_npb=$(echo "$output" | grep "Version         =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "N/A")
  
    echo "$verification,$mops,$time,$class_npb_reported,$size_reported,$total_threads_reported,$iterations_reported,$operation_type,$version_npb"
}

parse_ep_output() {
    local output="$1"
    local verification="false"
    local mflops="0.0"
    local time="0.0"
    local class_npb_reported="N/A"
    local size_reported="N/A"
    local total_threads_reported="0"
    local iterations_reported="0"
    local operation_type="N/A"
    local version_npb="N/A"
    
    if echo "$output" | grep -q "VERIFICATION SUCCESSFUL"; then
        verification="true"
    fi
    
    mflops=$(echo "$output" | grep "Mop/s total" | awk -F'=' '{print $2}' | tr -d ' ' || echo "0.0")
    time=$(echo "$output" | grep "Time in seconds" | awk -F'=' '{print $2}' | tr -d ' ' || echo "0.0")
    class_npb_reported=$(echo "$output" | grep "class_npb       =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "N/A")
    size_reported=$(echo "$output" | grep "Size            =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "N/A")
    total_threads_reported=$(echo "$output" | grep "Total threads   =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "0")
    iterations_reported=$(echo "$output" | grep "Iterations      =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "0")
    operation_type=$(echo "$output" | grep "Operation type  =" | awk -F'=' '{print $2}' | sed 's/^ *//' | sed 's/ *$//' || echo "N/A")
    version_npb=$(echo "$output" | grep "Version         =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "N/A")
  
    echo "$verification,$mflops,$time,$class_npb_reported,$size_reported,$total_threads_reported,$iterations_reported,$operation_type,$version_npb"
}

parse_cg_output() {
    local output="$1"
    local verification="false"
    local mflops="0.0"
    local time="0.0"
    local class_npb_reported="N/A"
    local size_reported="N/A"
    local total_threads_reported="0"
    local iterations_reported="0"
    local operation_type="N/A"
    local version_npb="N/A"
    
    if echo "$output" | grep -q "VERIFICATION SUCCESSFUL"; then
        verification="true"
    fi
    
    mflops=$(echo "$output" | grep "Mop/s total" | awk -F'=' '{print $2}' | tr -d ' ' || echo "0.0")
    time=$(echo "$output" | grep "Time in seconds" | awk -F'=' '{print $2}' | tr -d ' ' || echo "0.0")
    class_npb_reported=$(echo "$output" | grep "class_npb       =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "N/A")
    size_reported=$(echo "$output" | grep "Size            =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "N/A")
    total_threads_reported=$(echo "$output" | grep "Total threads   =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "0")
    iterations_reported=$(echo "$output" | grep "Iterations      =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "0")
    operation_type=$(echo "$output" | grep "Operation type  =" | awk -F'=' '{print $2}' | sed 's/^ *//' | sed 's/ *$//' || echo "N/A")
    version_npb=$(echo "$output" | grep "Version         =" | awk -F'=' '{print $2}' | tr -d ' ' || echo "N/A")
  
    echo "$verification,$mflops,$time,$class_npb_reported,$size_reported,$total_threads_reported,$iterations_reported,$operation_type,$version_npb"

}

run_benchmark() {
    local class="$1"
    local threads="$2"
    local iteration="$3"
    
    local start_time=$(date +%s.%N)
    local memory_before=$(get_memory_usage)
    
    local output
    local exit_code
    
    output=$($BINARY_PATH $class $threads 2>&1)
    exit_code=$?
    
    local end_time=$(date +%s.%N)
    local memory_after=$(get_memory_usage)
    local execution_time=$(echo "$end_time - $start_time" | bc -l)
    
    if [ $exit_code -ne 0 ]; then
        echo "ERROR: Benchmark failed with exit code $exit_code"
        return 1
    fi
    
    local parsed=$(parse_is_output "$output")
    local verification=$(echo "$parsed" | cut -d',' -f1)
    local mflops=$(echo "$parsed" | cut -d',' -f2)
    local reported_time=$(echo "$parsed" | cut -d',' -f3)
    local class_npb_reported=$(echo "$parsed" | cut -d',' -f4)
    local size_reported=$(echo "$parsed" | cut -d',' -f5)
    local total_threads_reported=$(echo "$parsed" | cut -d',' -f6)
    local iterations_reported=$(echo "$parsed" | cut -d',' -f7)
    local operation_type=$(echo "$parsed" | cut -d',' -f8)
    local version_npb=$(echo "$parsed" | cut -d',' -f9)
    
    local memory_used=$((memory_after - memory_before))
    if [ $memory_used -lt 0 ]; then
        memory_used=0
    fi
    
    local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%S.%3NZ")
    
    echo "{"
    echo "  \"class\": \"$class\","
    echo "  \"threads\": $threads,"
    echo "  \"iteration\": $iteration,"
    echo "  \"execution_time_seconds\": $execution_time,"
    echo "  \"reported_time_seconds\": $reported_time,"
    echo "  \"mflops\": $mflops,"
    echo "  \"verification_passed\": $verification,"
    echo "  \"memory_delta_kb\": $memory_used,"
    echo "  \"timestamp\": \"$timestamp\","
    echo "  \"exit_code\": $exit_code,"
    echo "  \"class_npb_reported\": \"$class_npb_reported\","
    echo "  \"size_reported\": \"$size_reported\","
    echo "  \"total_threads_reported\": $total_threads_reported,"
    echo "  \"iterations_reported\": $iterations_reported,"
    echo "  \"operation_type\": \"$operation_type\","
    echo "  \"version_npb\": \"$version_npb\""
    echo "}"
}

THREAD_COUNTS=""
for ((i=1; i<=MAX_THREADS; i++)); do
    THREAD_COUNTS="$THREAD_COUNTS $i"
done
if [ $MAX_THREADS -ne 1 ] && [ $MAX_THREADS -ne 2 ] && [ $MAX_THREADS -ne 4 ] && [ $MAX_THREADS -ne 8 ]; then
    THREAD_COUNTS="$THREAD_COUNTS $MAX_THREADS"
fi

echo "Thread counts to test:$THREAD_COUNTS"
echo ""

SYSTEM_INFO=$(get_system_info)
echo "System Information:"
echo "$SYSTEM_INFO" | jq . 2>/dev/null || echo "$SYSTEM_INFO"
echo ""

TOTAL_RUNS=$((${#CLASSES} * $(echo $THREAD_COUNTS | wc -w) * ITERATIONS))
CURRENT_RUN=0

echo "Starting $TOTAL_RUNS benchmark runs..."
echo ""

echo "[" > "$OUTPUT_DIR/benchmark_results.json"
echo "class,threads,iteration,execution_time_seconds,reported_time_seconds,mflops,verification_passed,memory_delta_kb,timestamp,exit_code" > "$OUTPUT_DIR/benchmark_results.csv"

FIRST_RESULT=true

for ((c=0; c<${#CLASSES}; c++)); do
    CLASS=${CLASSES:$c:1}
    
    echo "=== Testing Class $CLASS ==="
    
    for THREADS in $THREAD_COUNTS; do
        echo "  Threads: $THREADS"
        
        for ((iter=1; iter<=ITERATIONS; iter++)); do
            CURRENT_RUN=$((CURRENT_RUN + 1))
            printf "    Iteration %d/%d (Run %d/%d): " $iter $ITERATIONS $CURRENT_RUN $TOTAL_RUNS
            
            RESULT=$(run_benchmark "$CLASS" "$THREADS" "$iter")
            
            if [ $? -eq 0 ]; then
                VERIFICATION=$(echo "$RESULT" | jq -r '.verification_passed' 2>/dev/null || echo "$RESULT" | grep '"verification_passed"' | cut -d':' -f2 | tr -d ' ",')
                MFLOPS=$(echo "$RESULT" | jq -r '.mflops' 2>/dev/null || echo "$RESULT" | grep '"mflops"' | cut -d':' -f2 | tr -d ' ",')
                EXEC_TIME=$(echo "$RESULT" | jq -r '.execution_time_seconds' 2>/dev/null || echo "$RESULT" | grep '"execution_time_seconds"' | cut -d':' -f2 | tr -d ' ",')
                
                if [ "$VERIFICATION" = "true" ]; then
                    printf "✓ %.3fs, %.2f MFLOPS\n" "$EXEC_TIME" "$MFLOPS"
                else
                    printf "✗ %.3fs, %.2f MFLOPS (FAILED VERIFICATION)\n" "$EXEC_TIME" "$MFLOPS"
                fi
                
                if [ "$FIRST_RESULT" = true ]; then
                    FIRST_RESULT=false
                else
                    echo "," >> "$OUTPUT_DIR/benchmark_results.json"
                fi
                echo "$RESULT" >> "$OUTPUT_DIR/benchmark_results.json"
                                # Extract values for CSV, quoting strings
                CSV_REPORTED_TIME_S=$(echo "$RESULT" | jq -r '.reported_time_seconds')
                CSV_MEMORY_DELTA_KB=$(echo "$RESULT" | jq -r '.memory_delta_kb')
                CSV_TIMESTAMP="\"$(echo "$RESULT" | jq -r '.timestamp')\""
                CSV_EXIT_CODE=$(echo "$RESULT" | jq -r '.exit_code')
                CSV_CLASS_NPB_REPORTED="\"$(echo "$RESULT" | jq -r '.class_npb_reported')\""
                CSV_SIZE_REPORTED="\"$(echo "$RESULT" | jq -r '.size_reported')\""
                CSV_TOTAL_THREADS_REPORTED=$(echo "$RESULT" | jq -r '.total_threads_reported')
                CSV_ITERATIONS_REPORTED=$(echo "$RESULT" | jq -r '.iterations_reported')
                CSV_OPERATION_TYPE="\"$(echo "$RESULT" | jq -r '.operation_type')\""
                CSV_VERSION_NPB="\"$(echo "$RESULT" | jq -r '.version_npb')\""

                echo "$CLASS,$THREADS,$iter,$EXEC_TIME,$CSV_REPORTED_TIME_S,$MFLOPS,$VERIFICATION,$CSV_MEMORY_DELTA_KB,$CSV_TIMESTAMP,$CSV_EXIT_CODE,$CSV_CLASS_NPB_REPORTED,$CSV_SIZE_REPORTED,$CSV_TOTAL_THREADS_REPORTED,$CSV_ITERATIONS_REPORTED,$CSV_OPERATION_TYPE,$CSV_VERSION_NPB" >> "$OUTPUT_DIR/benchmark_results.csv"
                
            else
                printf "✗ FAILED\n"
            fi
        done
        echo ""
    done
done

echo "]" >> "$OUTPUT_DIR/benchmark_results.json"

echo "{"                                                    > "$OUTPUT_DIR/system_info.json"
echo "  \"benchmark_info\": {"                            >> "$OUTPUT_DIR/system_info.json"
echo "    \"binary_path\": \"$BINARY_PATH\","             >> "$OUTPUT_DIR/system_info.json"
echo "    \"classes_tested\": \"$CLASSES\","               >> "$OUTPUT_DIR/system_info.json"
echo "    \"max_threads\": $MAX_THREADS,"                  >> "$OUTPUT_DIR/system_info.json"
echo "    \"iterations_per_config\": $ITERATIONS,"         >> "$OUTPUT_DIR/system_info.json"
echo "    \"total_runs\": $TOTAL_RUNS,"                    >> "$OUTPUT_DIR/system_info.json"
echo "    \"thread_counts\": [$(echo $THREAD_COUNTS | sed 's/ /,/g')]," >> "$OUTPUT_DIR/system_info.json"
echo "    \"timestamp\": \"$(date -u +"%Y-%m-%dT%H:%M:%S.%3NZ")\""  >> "$OUTPUT_DIR/system_info.json"
echo "  },"                                                >> "$OUTPUT_DIR/system_info.json"
echo "  \"system_info\": $SYSTEM_INFO"                     >> "$OUTPUT_DIR/system_info.json"
echo "}"                                                   >> "$OUTPUT_DIR/system_info.json"

echo "=== BENCHMARK COMPLETED ==="
echo "Results saved to:"
echo "  JSON: $OUTPUT_DIR/benchmark_results.json"
echo "  CSV:  $OUTPUT_DIR/benchmark_results.csv"
echo "  System Info: $OUTPUT_DIR/system_info.json"
echo ""

if command -v jq >/dev/null 2>&1; then
    echo "=== QUICK SUMMARY ==="
    TOTAL_RESULTS=$(jq length "$OUTPUT_DIR/benchmark_results.json")
    SUCCESSFUL=$(jq '[.[] | select(.verification_passed == true)] | length' "$OUTPUT_DIR/benchmark_results.json")
    FAILED=$((TOTAL_RESULTS - SUCCESSFUL))
    
    echo "Total runs: $TOTAL_RESULTS"
    echo "Successful verifications: $SUCCESSFUL"
    echo "Failed verifications: $FAILED"
    
    if [ $SUCCESSFUL -gt 0 ]; then
        MIN_TIME=$(jq '[.[] | select(.verification_passed == true) | .execution_time_seconds] | min' "$OUTPUT_DIR/benchmark_results.json")
        MAX_TIME=$(jq '[.[] | select(.verification_passed == true) | .execution_time_seconds] | max' "$OUTPUT_DIR/benchmark_results.json")
        AVG_TIME=$(jq '[.[] | select(.verification_passed == true) | .execution_time_seconds] | add / length' "$OUTPUT_DIR/benchmark_results.json")
        
        MIN_MFLOPS=$(jq '[.[] | select(.verification_passed == true) | .mflops] | min' "$OUTPUT_DIR/benchmark_results.json")
        MAX_MFLOPS=$(jq '[.[] | select(.verification_passed == true) | .mflops] | max' "$OUTPUT_DIR/benchmark_results.json")
        AVG_MFLOPS=$(jq '[.[] | select(.verification_passed == true) | .mflops] | add / length' "$OUTPUT_DIR/benchmark_results.json")
        
        printf "Execution time: %.3fs (min) / %.3fs (avg) / %.3fs (max)\n" "$MIN_TIME" "$AVG_TIME" "$MAX_TIME"
        printf "MFLOPS: %.2f (min) / %.2f (avg) / %.2f (max)\n" "$MIN_MFLOPS" "$AVG_MFLOPS" "$MAX_MFLOPS"
    fi
    echo ""
fi

echo "You can now analyze the data in Python with:"
echo "  import pandas as pd"
echo "  df = pd.read_csv('$OUTPUT_DIR/benchmark_results.csv')"
echo "  print(df.groupby(['class', 'threads']).agg({'execution_time_seconds': ['mean', 'std'], 'mflops': ['mean', 'std']}))"