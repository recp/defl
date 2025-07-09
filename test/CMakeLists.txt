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

# Fuzz testing executable
add_executable(test_fuzz EXCLUDE_FROM_ALL
    test_fuzz.c
)

target_include_directories(test_fuzz PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${HUFF_INCLUDE_DIR}
)

target_link_libraries(test_fuzz defl)

# Add math library if needed
if(NOT WIN32)
    target_link_libraries(test_fuzz m)
endif()

# Fuzz test target - runs standalone fuzzer
add_custom_target(fuzz
    COMMAND test_fuzz 10000
    DEPENDS test_fuzz
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running fuzz tests (10000 iterations)"
)

# Quick fuzz test
add_custom_target(fuzz_quick
    COMMAND test_fuzz 1000
    DEPENDS test_fuzz
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running quick fuzz tests (1000 iterations)"
)

# Long fuzz test
add_custom_target(fuzz_long
    COMMAND test_fuzz 100000
    DEPENDS test_fuzz
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running extended fuzz tests (100000 iterations)"
)

# LibFuzzer variant (if using clang with fuzzer support)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_executable(test_fuzz_libfuzzer EXCLUDE_FROM_ALL
        test_fuzz.c
    )
    
    target_include_directories(test_fuzz_libfuzzer PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${HUFF_INCLUDE_DIR}
    )
    
    target_compile_definitions(test_fuzz_libfuzzer PRIVATE LIBFUZZER)
    target_compile_options(test_fuzz_libfuzzer PRIVATE -fsanitize=fuzzer,address)
    target_link_libraries(test_fuzz_libfuzzer defl -fsanitize=fuzzer,address)
    
    add_custom_target(fuzz_libfuzzer
        COMMAND test_fuzz_libfuzzer -max_total_time=60 -max_len=10000
        DEPENDS test_fuzz_libfuzzer
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running libFuzzer for 60 seconds"
    )
endif()

# AFL variant
add_executable(test_fuzz_afl EXCLUDE_FROM_ALL
    test_fuzz.c
)

target_include_directories(test_fuzz_afl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${HUFF_INCLUDE_DIR}
)

target_compile_definitions(test_fuzz_afl PRIVATE AFL_PERSISTENT)
target_link_libraries(test_fuzz_afl defl)

# Add math library if needed
if(NOT WIN32)
    target_link_libraries(test_fuzz_afl m)
endif()

# Note: AFL requires special setup, this just builds the binary
add_custom_target(fuzz_afl_build
    DEPENDS test_fuzz_afl
    COMMENT "AFL fuzzer binary built as test_fuzz_afl. Run with: afl-fuzz -i input_dir -o output_dir ./test_fuzz_afl"
)

# Combined test target that runs everything
add_custom_target(test_all
    COMMAND ${CMAKE_COMMAND} -E echo "=== Running standard tests ==="
    COMMAND ${CMAKE_CTEST_COMMAND} -V
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "=== Running fuzz tests ==="
    COMMAND test_fuzz 1000
    DEPENDS test_files test_fuzz
    COMMENT "Running all tests including fuzzing"
)

# For convenience, provide instructions
message(STATUS "")
message(STATUS "Test targets enabled:")
message(STATUS "  make test          - Run tests with verbose output")
message(STATUS "  make check         - Same as 'make test' (verbose output)")
message(STATUS "  make run_tests     - Run tests with full output + failure details")
message(STATUS "  make test_direct   - Run test binary directly")
message(STATUS "  make test_perf     - Run performance analysis")
message(STATUS "  make test_all      - Run all tests including fuzz tests")
message(STATUS "  make gen_test_data - Generate test data files")
message(STATUS "")
message(STATUS "Fuzz testing targets:")
message(STATUS "  make fuzz          - Run fuzz tests (10,000 iterations)")
message(STATUS "  make fuzz_quick    - Quick fuzz test (1,000 iterations)")
message(STATUS "  make fuzz_long     - Extended fuzz test (100,000 iterations)")
message(STATUS "  make fuzz_afl_build - Build AFL fuzzer binary")
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(STATUS "  make fuzz_libfuzzer - Run libFuzzer (requires clang)")
endif()
message(STATUS "")