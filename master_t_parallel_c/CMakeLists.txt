cmake_minimum_required(VERSION 3.20)
project(npb_cg_cpp23 VERSION 1.0 LANGUAGES CXX)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find OpenMP package
find_package(OpenMP)

# Define the executable
add_executable(cg
    main.cpp
    cg.cpp
    utils.cpp
)

# Include directories
target_include_directories(cg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Compiler flags
target_compile_options(cg PRIVATE 
    -O3 
    -march=native
    $<$<CXX_COMPILER_ID:GNU>:-fopenmp>
    $<$<CXX_COMPILER_ID:Clang>:-fopenmp>
    $<$<CXX_COMPILER_ID:MSVC>:/openmp>
)

# Link with OpenMP if found
if(OpenMP_CXX_FOUND)
    target_link_libraries(cg PRIVATE OpenMP::OpenMP_CXX)
endif()

# Set output directory
set_target_properties(cg PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Add custom targets for running different problem classes
foreach(CLASS_TYPE S W A B C D E)
    add_custom_target(run_${CLASS_TYPE}
        COMMAND $<TARGET_FILE:cg> ${CLASS_TYPE}
        DEPENDS cg
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running CG benchmark with CLASS=${CLASS_TYPE}"
    )
endforeach()

# Installation
install(TARGETS cg
    RUNTIME DESTINATION bin
)

# Print information about the build
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "OpenMP Found: ${OpenMP_CXX_FOUND}")