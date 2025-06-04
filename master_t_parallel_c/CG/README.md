# NPB-CG C++23 Implementation

This is a modern C++23 implementation of the NAS Parallel Benchmarks (NPB) Conjugate Gradient (CG) benchmark. This version has been rewritten from the original Fortran-like style C++ to a more idiomatic C++ approach that leverages modern C++ features to enhance parallelism.

## Overview

The CG benchmark tests both regular and irregular memory access patterns through a conjugate gradient method used to compute an approximation to the smallest eigenvalue of a large, sparse, symmetric positive definite matrix.

## Features

- Modern C++23 implementation with strong type safety
- Improved parallelism using:
  - Parallel algorithms from C++17/20
  - Thread pools and futures for async operations
  - Data-oriented design for cache efficiency
  - Parallel reductions and transformations
- Clean separation of concerns through RAII and better encapsulation
- Use of standard library containers and algorithms

## Requirements

- A C++23 compliant compiler (GCC 12+, Clang 16+, or MSVC 19.30+)
- CMake 3.20 or higher
- OpenMP support (optional but recommended)

## Compilation

### Using CMake

```bash
mkdir build
cd build
cmake ..
make
```

### Manual Compilation

```bash
g++ -std=c++23 -O3 -march=native -fopenmp -o cg main.cpp cg.cpp utils.cpp
```

## Usage

The executable is typically found in `./bin/cg` after compilation with CMake, or `./cg` if compiled manually as per the example.

**Syntax:** `./bin/cg [CLASS] [NUM_THREADS]` (if using CMake build) or `./cg [CLASS] [NUM_THREADS]` (if manual build)

-   `CLASS` (optional): The problem class (S, W, A, B, C, D, E).
    -   If provided on the command line, it overrides any `CLASS` environment variable.
    -   If not provided on the command line, the `CLASS` environment variable is checked.
    -   If neither is set, defaults to 'A'.
-   `NUM_THREADS` (optional): The number of OpenMP threads to use.
    -   This argument is only considered if `CLASS` is also provided as the first command-line argument.
    -   If provided and valid, this setting takes precedence over the `OMP_NUM_THREADS` environment variable.
    -   If not provided on the command line or if the value is invalid, OpenMP will use the value of the `OMP_NUM_THREADS` environment variable, or the system default if `OMP_NUM_THREADS` is not set.

**Examples:**

Assume the executable is `./bin/cg`.

```bash
# Run with default class A, default OpenMP threads
./bin/cg

# Run with class S, default OpenMP threads
./bin/cg S

# Run with class A, using 4 OpenMP threads
./bin/cg A 4

# Run with class B (from env var), default OpenMP threads
CLASS=B ./bin/cg

# Run with class C (from command line, overrides env var if set), using 2 OpenMP threads
CLASS=A ./bin/cg C 2

# Run with class W, threads set by OMP_NUM_THREADS environment variable
OMP_NUM_THREADS=8 ./bin/cg W

# Run with class D, command-line threads override OMP_NUM_THREADS
OMP_NUM_THREADS=8 ./bin/cg D 4 # Will use 4 threads
```

## Problem Classes

| Class | Matrix Size (NA) | Non-zeros per row | Iterations | Shift |
|-------|------------------|-------------------|------------|-------|
| S     | 1,400            | 7                 | 15         | 10.0  |
| W     | 7,000            | 8                 | 15         | 12.0  |
| A     | 14,000           | 11                | 15         | 20.0  |
| B     | 75,000           | 13                | 75         | 60.0  |
| C     | 150,000          | 15                | 75         | 110.0 |
| D     | 1,500,000        | 21                | 100        | 500.0 |
| E     | 9,000,000        | 26                | 100        | 1500.0|

## Performance Comparison

This modern C++23 implementation shows significant performance improvements over the original NPB-CPP implementation, especially for larger problem sizes (Classes C, D, and E) due to better cache utilization and parallelism.

## Code Structure

- `main.cpp`: Entry point, handles command line arguments and benchmark setup
- `cg.hpp`, `cg.cpp`: Core implementation of the CG algorithm
- `utils.hpp`, `utils.cpp`: Utility functions for timing, random number generation, and result reporting

## Modernizations

1. **Memory Management**:
   - RAII principles with proper scope-bound lifetime
   - Use of `std::vector` instead of raw arrays
   - Smart pointers where appropriate
   - Contiguous memory layouts for better cache performance

2. **Parallelism**:
   - Modern parallel algorithms with `std::execution` policies
   - Task-based parallelism with futures and continuations
   - Explicit thread control with barriers and thread pools
   - Reduction operations using parallel techniques

3. **C++ Features**:
   - Structured bindings and tuples
   - Concepts for better type constraints
   - Ranges for clearer data transformations
   - constexpr evaluation where possible

## Acknowledgments

Based on the NAS Parallel Benchmarks, originally developed by NASA. The NPB-CPP version was created by the Parallel Applications Modelling Group (GMAP) at PUCRS, Brazil.