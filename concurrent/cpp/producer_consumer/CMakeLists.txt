cmake_minimum_required(VERSION 3.20)

project(ConcurrencyBenchmark
    VERSION 1.0.0
    DESCRIPTION "C++ Concurrency Mechanisms Benchmark"
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
    
    # Enable C++20 threading features
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    
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
    
    # Enable C++20 threading features
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mcpu=native")
    
    # For Apple Clang, we might need to fall back to compatibility mode
    message(WARNING "Apple Clang may not support all C++20 threading features. Consider using Homebrew LLVM.")
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive-")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Ox /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
endif()

find_package(Threads REQUIRED)

if(APPLE)
    find_library(CORE_FOUNDATION CoreFoundation)
    if(NOT CORE_FOUNDATION)
        message(FATAL_ERROR "CoreFoundation framework not found")
    endif()
endif()

add_executable(concurrency_benchmark
    main.cpp
)

target_link_libraries(concurrency_benchmark
    PRIVATE
    Threads::Threads
)

if(WIN32)
    target_link_libraries(concurrency_benchmark PRIVATE psapi)
elseif(APPLE)
    target_link_libraries(concurrency_benchmark PRIVATE ${CORE_FOUNDATION})
endif()

target_compile_features(concurrency_benchmark PRIVATE cxx_std_20)

target_compile_definitions(concurrency_benchmark PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(concurrency_benchmark PRIVATE _GNU_SOURCE)
endif()

# Check for C++20 threading support
include(CheckCXXSourceCompiles)

set(CMAKE_REQUIRED_FLAGS "-std=c++20")
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -stdlib=libc++")
endif()

check_cxx_source_compiles("
#include <thread>
#include <jthread>
int main() {
    std::jthread t([](){});
    return 0;
}
" HAS_JTHREAD)

check_cxx_source_compiles("
#include <latch>
int main() {
    std::latch l(1);
    l.arrive_and_wait();
    return 0;
}
" HAS_LATCH)

check_cxx_source_compiles("
#include <barrier>
int main() {
    std::barrier b(1);
    b.arrive_and_wait();
    return 0;
}
" HAS_BARRIER)

if(HAS_JTHREAD AND HAS_LATCH AND HAS_BARRIER)
    target_compile_definitions(concurrency_benchmark PRIVATE HAS_FULL_CPP20_THREADING)
    message(STATUS "Full C++20 threading support detected")
else()
    message(STATUS "Limited C++20 threading support - using compatibility implementations")
    if(NOT HAS_JTHREAD)
        message(STATUS "  std::jthread not available")
    else()
        target_compile_definitions(concurrency_benchmark PRIVATE HAS_JTHREAD)
    endif()
    if(NOT HAS_LATCH)
        message(STATUS "  std::latch not available")
    else()
        target_compile_definitions(concurrency_benchmark PRIVATE HAS_LATCH)
    endif()
    if(NOT HAS_BARRIER)
        message(STATUS "  std::barrier not available")
    else()
        target_compile_definitions(concurrency_benchmark PRIVATE HAS_BARRIER)
    endif()
endif()

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_PROFILING "Enable profiling support" OFF)

if(ENABLE_ASAN AND NOT ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(concurrency_benchmark PRIVATE -fsanitize=address)
        target_link_options(concurrency_benchmark PRIVATE -fsanitize=address)
    endif()
endif()

if(ENABLE_TSAN AND NOT ENABLE_ASAN)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(concurrency_benchmark PRIVATE -fsanitize=thread)
        target_link_options(concurrency_benchmark PRIVATE -fsanitize=thread)
    endif()
endif()

if(ENABLE_UBSAN)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(concurrency_benchmark PRIVATE -fsanitize=undefined)
        target_link_options(concurrency_benchmark PRIVATE -fsanitize=undefined)
    endif()
endif()

if(ENABLE_PROFILING)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(concurrency_benchmark PRIVATE -pg)
        target_link_options(concurrency_benchmark PRIVATE -pg)
    endif()
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    message(STATUS "Building for ARM64 architecture")
    target_compile_definitions(concurrency_benchmark PRIVATE ARM64_ARCHITECTURE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    message(STATUS "Building for x86_64 architecture")
    target_compile_definitions(concurrency_benchmark PRIVATE X86_64_ARCHITECTURE)
endif()

set_target_properties(concurrency_benchmark PROPERTIES
    OUTPUT_NAME "benchmark"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS concurrency_benchmark
    RUNTIME DESTINATION bin
)

add_custom_target(run_benchmark
    COMMAND $<TARGET_FILE:concurrency_benchmark> 8 1000
    DEPENDS concurrency_benchmark
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running concurrency benchmark with 8 threads and 1000 items"
)

add_custom_target(run_benchmark_stress
    COMMAND $<TARGET_FILE:concurrency_benchmark> 16 10000
    DEPENDS concurrency_benchmark
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running stress test with 16 threads and 10000 items"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_custom_target(run_benchmark_profile
        COMMAND perf record -g $<TARGET_FILE:concurrency_benchmark> 8 5000
        DEPENDS concurrency_benchmark
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running benchmark with perf profiling"
    )
elseif(APPLE)
    add_custom_target(run_benchmark_profile
        COMMAND xcrun xctrace record --template 'Time Profiler' --launch -- $<TARGET_FILE:concurrency_benchmark> 8 5000
        DEPENDS concurrency_benchmark
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running benchmark with Instruments profiling"
    )
endif()

message(STATUS "")
message(STATUS "=== Concurrency Benchmark Configuration ===")
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
message(STATUS "")
message(STATUS "Build options:")
message(STATUS "  ENABLE_ASAN: ${ENABLE_ASAN}")
message(STATUS "  ENABLE_TSAN: ${ENABLE_TSAN}")
message(STATUS "  ENABLE_UBSAN: ${ENABLE_UBSAN}")
message(STATUS "  ENABLE_PROFILING: ${ENABLE_PROFILING}")
message(STATUS "")
message(STATUS "Usage after build:")
message(STATUS "  make run_benchmark          # Standard test")
message(STATUS "  make run_benchmark_stress    # Stress test")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "  make run_benchmark_profile   # With perf profiling")
elseif(APPLE)
    message(STATUS "  make run_benchmark_profile   # With Instruments profiling")
endif()
message(STATUS "===========================================")