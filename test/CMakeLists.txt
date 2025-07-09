# Test configuration for defl

# Main file-based test using generated data
add_executable(test_files
    test_files.c
)

target_include_directories(test_files PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${HUFF_INCLUDE_DIR}
)

target_link_libraries(test_files defl)

# Add math library for performance tests
if(NOT WIN32)
    target_link_libraries(test_files m)
endif()

# Add tests - run from test directory where data files are located
add_test(NAME file_based_tests 
    COMMAND test_files
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Always show test output by setting test properties
set_tests_properties(file_based_tests PROPERTIES
    # This ensures output is always shown, not just on failure
    PASS_REGULAR_EXPRESSION ".*"
    # Set timeout
    TIMEOUT 300
)

# Set environment to always show output
set(ENV{CTEST_OUTPUT_ON_FAILURE} "1")

# Main test target (use different name since "test" is reserved)
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -V --output-on-failure
    DEPENDS test_files
    COMMENT "Running DEFLATE tests with file-based data"
)

# Add 'make check' as an alias for 'make test' with same verbose behavior
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} -V $(ARGS)
    DEPENDS test_files
    COMMENT "Running tests (alias for 'make test')"
)

# Direct test execution with full output
add_custom_target(test_direct
    COMMAND test_files
    DEPENDS test_files
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running tests directly with all output"
)

# Performance test target  
add_custom_target(test_perf
    COMMAND test_files | grep -E "(MB/s|compression)"
    DEPENDS test_files
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running performance analysis"
)

# Generate test data targets
add_custom_target(gen_test_data
    COMMAND python3 gendata.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data
    COMMENT "Generating test data (raw and compressed files)"
)

# Clean test data
add_custom_target(clean_test_data
    COMMAND rm -rf raw compressed
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data
    COMMENT "Cleaning generated test data"
)

# Benchmark executable (optional)
add_executable(defl_bench EXCLUDE_FROM_ALL
    benchmark.c
)

target_include_directories(defl_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${HUFF_INCLUDE_DIR}
)

target_link_libraries(defl_bench defl)

# For convenience, provide instructions
message(STATUS "")
message(STATUS "Test targets enabled:")
message(STATUS "  make test          - Run tests with verbose output")
message(STATUS "  make check         - Same as 'make test' (verbose output)")
message(STATUS "  make run_tests     - Run tests with full output + failure details")
message(STATUS "  make test_direct   - Run test binary directly")
message(STATUS "  make test_perf     - Run performance analysis")
message(STATUS "  make gen_test_data - Generate test data files")
message(STATUS "")