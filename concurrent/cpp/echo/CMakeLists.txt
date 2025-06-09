cmake_minimum_required(VERSION 3.20)

project(AsyncBenchmark
    VERSION 1.0.0
    DESCRIPTION "C++ Async Mechanisms Benchmark vs Rust Tokio"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Try to find modern Clang first
if(APPLE)
    # Check for Homebrew LLVM first
    if(EXISTS "/opt/homebrew/opt/llvm/bin/clang++")
        set(CMAKE_CXX_COMPILER "/opt/homebrew/opt/llvm/bin/clang++")
        set(CMAKE_C_COMPILER "/opt/homebrew/opt/llvm/bin/clang")
        message(STATUS "Using Homebrew LLVM: ${CMAKE_CXX_COMPILER}")
    elseif(EXISTS "/usr/local/opt/llvm/bin/clang++")
        set(CMAKE_CXX_COMPILER "/usr/local/opt/llvm/bin/clang++")
        set(CMAKE_C_COMPILER "/usr/local/opt/llvm/bin/clang")
        message(STATUS "Using Homebrew LLVM: ${CMAKE_CXX_COMPILER}")
    else()
        message(STATUS "Using system Apple Clang: ${CMAKE_CXX_COMPILER}")
    endif()
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
    
    # Enable C++20 threading and networking features
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    
    # Enable coroutines support if available
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
    
    # Use libc++ for modern C++20 features
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    
    if(APPLE)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mcpu=native")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
    endif()
    
    # Enable C++20 threading and coroutines
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    
    # Use correct coroutines flag based on Clang version
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines-ts")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mcpu=native")
    
    # For Apple Clang, we might need to fall back to compatibility mode
    message(WARNING "Apple Clang may not support all C++20 features. Consider using Homebrew LLVM.")
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive-")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Ox /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
    
    # Enable coroutines for MSVC
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /await")
endif()

# Find required packages
find_package(Threads REQUIRED)

# Platform-specific libraries
if(WIN32)
    # Windows socket libraries
    set(PLATFORM_LIBS ws2_32 psapi)
elseif(APPLE)
    # macOS frameworks
    find_library(CORE_FOUNDATION CoreFoundation)
    if(NOT CORE_FOUNDATION)
        message(FATAL_ERROR "CoreFoundation framework not found")
    endif()
    set(PLATFORM_LIBS ${CORE_FOUNDATION})
elseif(UNIX)
    # Linux libraries
    set(PLATFORM_LIBS)
endif()

# Create main executable
add_executable(async_benchmark
    main.cpp
)

target_link_libraries(async_benchmark
    PRIVATE
    Threads::Threads
    ${PLATFORM_LIBS}
)

target_compile_features(async_benchmark PRIVATE cxx_std_20)

target_compile_definitions(async_benchmark PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(async_benchmark PRIVATE _GNU_SOURCE)
endif()

# Check for C++20 feature support
include(CheckCXXSourceCompiles)

set(CMAKE_REQUIRED_FLAGS "-std=c++20")
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -stdlib=libc++")
endif()

# Check for std::jthread
check_cxx_source_compiles("
#include <thread>
int main() {
    std::jthread t([](){});
    return 0;
}
" HAS_JTHREAD)

# Check for std::latch
check_cxx_source_compiles("
#include <latch>
int main() {
    std::latch l(1);
    l.arrive_and_wait();
    return 0;
}
" HAS_LATCH)

# Check for std::barrier
check_cxx_source_compiles("
#include <barrier>
int main() {
    std::barrier b(1);
    b.arrive_and_wait();
    return 0;
}
" HAS_BARRIER)

# Check for std::packaged_task
check_cxx_source_compiles("
#include <future>
#include <functional>
int main() {
    std::packaged_task<int()> task([]() { return 42; });
    auto future = task.get_future();
    return 0;
}
" HAS_PACKAGED_TASK)

# Check for std::async
check_cxx_source_compiles("
#include <future>
int main() {
    auto future = std::async(std::launch::async, []() { return 42; });
    return 0;
}
" HAS_ASYNC)

# Check for std::atomic with memory ordering
check_cxx_source_compiles("
#include <atomic>
int main() {
    std::atomic<int> a{0};
    a.fetch_add(1, std::memory_order_relaxed);
    return 0;
}
" HAS_ATOMIC_OPERATIONS)

# Check for coroutines (experimental)
check_cxx_source_compiles("
#include <coroutine>
struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};
Task test() { co_return; }
int main() { return 0; }
" HAS_COROUTINES)

# Set feature macros based on detected capabilities
if(HAS_JTHREAD)
    target_compile_definitions(async_benchmark PRIVATE HAS_JTHREAD)
    message(STATUS "✓ std::jthread support detected")
else()
    message(STATUS "✗ std::jthread not available - using std::thread")
endif()

if(HAS_LATCH)
    target_compile_definitions(async_benchmark PRIVATE HAS_LATCH)
    message(STATUS "✓ std::latch support detected")
else()
    message(STATUS "✗ std::latch not available - using custom implementation")
endif()

if(HAS_BARRIER)
    target_compile_definitions(async_benchmark PRIVATE HAS_BARRIER)
    message(STATUS "✓ std::barrier support detected")
else()
    message(STATUS "✗ std::barrier not available - using custom implementation")
endif()

if(HAS_PACKAGED_TASK)
    target_compile_definitions(async_benchmark PRIVATE HAS_PACKAGED_TASK)
    message(STATUS "✓ std::packaged_task support detected")
endif()

if(HAS_ASYNC)
    target_compile_definitions(async_benchmark PRIVATE HAS_ASYNC)
    message(STATUS "✓ std::async support detected")
endif()

if(HAS_ATOMIC_OPERATIONS)
    target_compile_definitions(async_benchmark PRIVATE HAS_ATOMIC_OPERATIONS)
    message(STATUS "✓ std::atomic operations support detected")
endif()

if(HAS_COROUTINES)
    target_compile_definitions(async_benchmark PRIVATE HAS_COROUTINES)
    message(STATUS "✓ C++20 coroutines support detected")
else()
    message(STATUS "✗ C++20 coroutines not available")
endif()

# Check for full async support
if(HAS_JTHREAD AND HAS_LATCH AND HAS_BARRIER AND HAS_PACKAGED_TASK AND HAS_ASYNC)
    target_compile_definitions(async_benchmark PRIVATE HAS_FULL_ASYNC_SUPPORT)
    message(STATUS "✓ Full C++20 async support detected")
else()
    message(STATUS "⚠ Limited C++20 async support - using compatibility implementations")
endif()

# Debug and optimization options
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_PROFILING "Enable profiling support" OFF)
option(ENABLE_BENCHMARKING "Enable benchmarking optimizations" ON)

# AddressSanitizer (cannot be used with ThreadSanitizer)
if(ENABLE_ASAN AND NOT ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(async_benchmark PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(async_benchmark PRIVATE -fsanitize=address)
        message(STATUS "✓ AddressSanitizer enabled")
    endif()
endif()

# ThreadSanitizer (cannot be used with AddressSanitizer)
if(ENABLE_TSAN AND NOT ENABLE_ASAN)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(async_benchmark PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(async_benchmark PRIVATE -fsanitize=thread)
        message(STATUS "✓ ThreadSanitizer enabled")
    endif()
endif()

# UndefinedBehaviorSanitizer
if(ENABLE_UBSAN)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(async_benchmark PRIVATE -fsanitize=undefined)
        target_link_options(async_benchmark PRIVATE -fsanitize=undefined)
        message(STATUS "✓ UndefinedBehaviorSanitizer enabled")
    endif()
endif()

# Profiling support
if(ENABLE_PROFILING)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(async_benchmark PRIVATE -pg -fno-omit-frame-pointer)
        target_link_options(async_benchmark PRIVATE -pg)
        message(STATUS "✓ Profiling support enabled")
    endif()
endif()

# Benchmarking optimizations
if(ENABLE_BENCHMARKING AND CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(async_benchmark PRIVATE -ffast-math -funroll-loops)
        message(STATUS "✓ Benchmarking optimizations enabled")
    endif()
endif()

# Architecture detection
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    message(STATUS "Building for ARM64 architecture")
    target_compile_definitions(async_benchmark PRIVATE ARM64_ARCHITECTURE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    message(STATUS "Building for x86_64 architecture")
    target_compile_definitions(async_benchmark PRIVATE X86_64_ARCHITECTURE)
endif()

# Set target properties
set_target_properties(async_benchmark PROPERTIES
    OUTPUT_NAME "async_benchmark"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Install rules
install(TARGETS async_benchmark
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)

# Custom targets for running benchmarks
add_custom_target(run_basic
    COMMAND $<TARGET_FILE:async_benchmark>
    DEPENDS async_benchmark
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running basic async benchmark"
)

add_custom_target(run_stress
    COMMAND $<TARGET_FILE:async_benchmark> --stress
    DEPENDS async_benchmark
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running stress test with high concurrency"
)

add_custom_target(run_producer_consumer
    COMMAND $<TARGET_FILE:async_benchmark> --test=producer-consumer --producers=8 --consumers=8 --items=10000
    DEPENDS async_benchmark
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running producer-consumer benchmark"
)

add_custom_target(run_echo_server
    COMMAND $<TARGET_FILE:async_benchmark> --test=echo-server --clients=100 --messages=1000
    DEPENDS async_benchmark
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running echo server benchmark"
)

# Platform-specific profiling targets
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_custom_target(run_profile_perf
        COMMAND perf record -g --call-graph=dwarf $<TARGET_FILE:async_benchmark>
        DEPENDS async_benchmark
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running benchmark with perf profiling"
    )
    
    add_custom_target(run_profile_valgrind
        COMMAND valgrind --tool=callgrind $<TARGET_FILE:async_benchmark>
        DEPENDS async_benchmark
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running benchmark with Valgrind profiling"
    )
elseif(APPLE)
    add_custom_target(run_profile_instruments
        COMMAND xcrun xctrace record --template 'Time Profiler' --launch -- $<TARGET_FILE:async_benchmark>
        DEPENDS async_benchmark
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running benchmark with Instruments profiling"
    )
    
    add_custom_target(run_profile_dtrace
        COMMAND sudo dtrace -n 'pid$target:::entry { @[probefunc] = count(); }' -c $<TARGET_FILE:async_benchmark>
        DEPENDS async_benchmark
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running benchmark with DTrace profiling"
    )
endif()

# Memory analysis targets
add_custom_target(run_memory_analysis
    COMMAND valgrind --tool=massif --detailed-freq=1 $<TARGET_FILE:async_benchmark>
    DEPENDS async_benchmark
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running memory usage analysis with Valgrind Massif"
)

# Thread safety analysis
if(ENABLE_TSAN)
    add_custom_target(run_thread_safety
        COMMAND $<TARGET_FILE:async_benchmark> --test=all
        DEPENDS async_benchmark
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running thread safety analysis with ThreadSanitizer"
    )
endif()

# Comparison target (requires Rust implementation)
add_custom_target(run_comparison
    COMMAND echo "Running C++ benchmark..." && $<TARGET_FILE:async_benchmark> > cpp_results.txt && echo "C++ results saved to cpp_results.txt"
    DEPENDS async_benchmark
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running C++ benchmark for comparison with Rust"
)

# Print configuration summary
message(STATUS "")
message(STATUS "=== Async Benchmark Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Compiler path: ${CMAKE_CXX_COMPILER}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "")
message(STATUS "C++20 Feature Support:")
message(STATUS "  std::jthread: ${HAS_JTHREAD}")
message(STATUS "  std::latch: ${HAS_LATCH}")
message(STATUS "  std::barrier: ${HAS_BARRIER}")
message(STATUS "  std::packaged_task: ${HAS_PACKAGED_TASK}")
message(STATUS "  std::async: ${HAS_ASYNC}")
message(STATUS "  std::atomic ops: ${HAS_ATOMIC_OPERATIONS}")
message(STATUS "  coroutines: ${HAS_COROUTINES}")
message(STATUS "")
message(STATUS "Build options:")
message(STATUS "  ENABLE_ASAN: ${ENABLE_ASAN}")
message(STATUS "  ENABLE_TSAN: ${ENABLE_TSAN}")
message(STATUS "  ENABLE_UBSAN: ${ENABLE_UBSAN}")
message(STATUS "  ENABLE_PROFILING: ${ENABLE_PROFILING}")
message(STATUS "  ENABLE_BENCHMARKING: ${ENABLE_BENCHMARKING}")
message(STATUS "")
message(STATUS "Available targets:")
message(STATUS "  make run_basic               # Basic benchmark")
message(STATUS "  make run_stress              # Stress test")
message(STATUS "  make run_producer_consumer   # Producer-consumer test")
message(STATUS "  make run_echo_server         # Echo server test")
message(STATUS "  make run_comparison          # Save results for comparison")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "  make run_profile_perf        # Profile with perf")
    message(STATUS "  make run_profile_valgrind    # Profile with Valgrind")
    message(STATUS "  make run_memory_analysis     # Memory analysis")
elseif(APPLE)
    message(STATUS "  make run_profile_instruments # Profile with Instruments")
    message(STATUS "  make run_profile_dtrace      # Profile with DTrace")
endif()
if(ENABLE_TSAN)
    message(STATUS "  make run_thread_safety       # Thread safety analysis")
endif()
message(STATUS "")
message(STATUS "Comparison with Rust Tokio:")
message(STATUS "  This benchmark tests equivalent async patterns")
message(STATUS "  to the Rust Tokio implementation for direct")
message(STATUS "  performance and resource usage comparison.")
message(STATUS "========================================")