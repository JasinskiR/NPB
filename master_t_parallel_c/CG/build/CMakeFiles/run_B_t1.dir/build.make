# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 4.0

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /opt/homebrew/bin/cmake

# The command to remove a file.
RM = /opt/homebrew/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Users/rafal/Desktop/All/Studia/master/3_term/Master_thesis/MasterT_Parallel/NPB/master_t_parallel_c

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/rafal/Desktop/All/Studia/master/3_term/Master_thesis/MasterT_Parallel/NPB/master_t_parallel_c/build

# Utility rule file for run_B_t1.

# Include any custom commands dependencies for this target.
include CMakeFiles/run_B_t1.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/run_B_t1.dir/progress.make

CMakeFiles/run_B_t1: bin/cg
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/rafal/Desktop/All/Studia/master/3_term/Master_thesis/MasterT_Parallel/NPB/master_t_parallel_c/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Running CG benchmark with CLASS=B and 1 threads"
	/Users/rafal/Desktop/All/Studia/master/3_term/Master_thesis/MasterT_Parallel/NPB/master_t_parallel_c/build/bin/cg B -t 1

CMakeFiles/run_B_t1.dir/codegen:
.PHONY : CMakeFiles/run_B_t1.dir/codegen

run_B_t1: CMakeFiles/run_B_t1
run_B_t1: CMakeFiles/run_B_t1.dir/build.make
.PHONY : run_B_t1

# Rule to build all files generated by this target.
CMakeFiles/run_B_t1.dir/build: run_B_t1
.PHONY : CMakeFiles/run_B_t1.dir/build

CMakeFiles/run_B_t1.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/run_B_t1.dir/cmake_clean.cmake
.PHONY : CMakeFiles/run_B_t1.dir/clean

CMakeFiles/run_B_t1.dir/depend:
	cd /Users/rafal/Desktop/All/Studia/master/3_term/Master_thesis/MasterT_Parallel/NPB/master_t_parallel_c/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/rafal/Desktop/All/Studia/master/3_term/Master_thesis/MasterT_Parallel/NPB/master_t_parallel_c /Users/rafal/Desktop/All/Studia/master/3_term/Master_thesis/MasterT_Parallel/NPB/master_t_parallel_c /Users/rafal/Desktop/All/Studia/master/3_term/Master_thesis/MasterT_Parallel/NPB/master_t_parallel_c/build /Users/rafal/Desktop/All/Studia/master/3_term/Master_thesis/MasterT_Parallel/NPB/master_t_parallel_c/build /Users/rafal/Desktop/All/Studia/master/3_term/Master_thesis/MasterT_Parallel/NPB/master_t_parallel_c/build/CMakeFiles/run_B_t1.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/run_B_t1.dir/depend

