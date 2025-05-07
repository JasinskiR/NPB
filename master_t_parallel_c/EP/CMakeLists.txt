cmake_minimum_required(VERSION 3.20)
project(npb_ep_cpp23 VERSION 1.0 LANGUAGES CXX)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Special handling for macOS and OpenMP
if(APPLE)
    # Try to find Homebrew OpenMP installation
    execute_process(
        COMMAND brew --prefix libomp
        OUTPUT_VARIABLE OPENMP_ROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    if(NOT OPENMP_ROOT)
        # Try standard locations
        set(OPENMP_ROOT "/opt/homebrew/opt/libomp")
        if(NOT EXISTS ${OPENMP_ROOT})
            set(OPENMP_ROOT "/usr/local/opt/libomp")
        endif()
    endif()
    
    if(EXISTS ${OPENMP_ROOT})
        message(STATUS "Found Homebrew OpenMP at: ${OPENMP_ROOT}")
        
        # Set variables manually for macOS
        set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp")
        set(OpenMP_CXX_LIB_NAMES "omp")
        set(OpenMP_omp_LIBRARY "${OPENMP_ROOT}/lib/libomp.dylib")
        set(OpenMP_CXX_INCLUDE_DIRS "${OPENMP_ROOT}/include")
        
        # Apply these settings globally
        include_directories(SYSTEM ${OpenMP_CXX_INCLUDE_DIRS})
        link_directories(${OPENMP_ROOT}/lib)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
        set(OpenMP_CXX_FOUND TRUE)
    else()
        message(FATAL_ERROR "OpenMP not found. Install with: brew install libomp")
    endif()
else()
    # Standard OpenMP for non-macOS
    find_package(OpenMP REQUIRED)
endif()

# Define the executable
add_executable(ep
    main.cpp
    utils.cpp
    ep.cpp
)

# Include directories
target_include_directories(ep PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenMP_CXX_INCLUDE_DIRS}
)

# Compiler flags
target_compile_options(ep PRIVATE 
    -O0
    -march=native
)

# Link with OpenMP
if(APPLE)
    target_link_libraries(ep PRIVATE ${OpenMP_omp_LIBRARY})
else()
    target_link_libraries(ep PRIVATE OpenMP::OpenMP_CXX)
endif()

# Set output directory
set_target_properties(ep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Add custom targets for running different problem classes
foreach(CLASS_TYPE S W A B C D E)
    add_custom_target(run_${CLASS_TYPE}
        COMMAND $<TARGET_FILE:ep> ${CLASS_TYPE}
        DEPENDS ep
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running ep benchmark with CLASS=${CLASS_TYPE}"
    )
    
    # Add thread count variants (1, 2, 4, 8, 16 threads)
    foreach(THREAD_COUNT 1 2 4 8 16)
        add_custom_target(run_${CLASS_TYPE}_t${THREAD_COUNT}
            COMMAND $<TARGET_FILE:ep> ${CLASS_TYPE} -t ${THREAD_COUNT}
            DEPENDS ep
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running ep benchmark with CLASS=${CLASS_TYPE} and ${THREAD_COUNT} threads"
        )
    endforeach()
endforeach()

# Installation
install(TARGETS ep
    RUNTIME DESTINATION bin
)

# Print information about the build
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "OpenMP Found: ${OpenMP_CXX_FOUND}")