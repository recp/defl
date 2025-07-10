cmake_minimum_required(VERSION 3.13)
project(defl
    VERSION 0.1.0
    HOMEPAGE_URL https://github.com/recp/unz
    DESCRIPTION "fast deflate inflate library"
    LANGUAGES C
)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(DEFAULT_BUILD_TYPE "Release")

# Options
option(DEFL_USE_TEST "Enable Tests" OFF)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Add compiler flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -O3 -ffast-math)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -DDEBUG=1)
    endif()
endif()

# Find required dependencies
find_path(HUFF_INCLUDE_DIR NAMES huff/huff.h 
    PATHS 
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/huff/include
        ../huff/include
    NO_DEFAULT_PATH
)
if(NOT HUFF_INCLUDE_DIR)
    message(FATAL_ERROR "huff library not found. Please ensure huff is available as a git submodule in deps/huff or at ../huff/include/huff/huff.h")
endif()

# Make HUFF_INCLUDE_DIR available to subdirectories
set(HUFF_INCLUDE_DIR ${HUFF_INCLUDE_DIR} CACHE PATH "Path to huff headers" FORCE)

# Create the main library
add_library(defl STATIC
    src/infl/infl.c
    src/infl/mem.c
    src/infl/stream.c
)

target_include_directories(defl
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${HUFF_INCLUDE_DIR}
        src
)

target_compile_definitions(defl PRIVATE
    UNZ_STATIC=1
    UNZ_EXPORTS=1
)

# Add POSIX feature test macros for alignment functions
if(NOT WIN32)
    target_compile_definitions(defl PRIVATE _GNU_SOURCE)
endif()

# Test Configuration
option(DEFL_USE_TEST "Enable Tests" OFF)

if(DEFL_USE_TEST)
    # Configure CTest to be verbose by default - must be before enable_testing()
    set(CMAKE_CTEST_ARGUMENTS -V)
    enable_testing()
    add_subdirectory(test)
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS defl
    EXPORT deflTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT deflTargets
    FILE deflTargets.cmake
    NAMESPACE defl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/defl
)

# Package configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deflConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/deflConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/defl
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/deflConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/deflConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/deflConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/defl
)

# Configuration Summary
message(STATUS "")
message(STATUS "defl Configuration:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
if(CMAKE_BUILD_TYPE)
    message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
endif()
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Tests enabled:  ${DEFL_USE_TEST}")
if(NOT DEFL_USE_TEST)
    message(STATUS "")
    message(STATUS "To enable tests, configure with: -DDEFL_USE_TEST=ON")
endif()
message(STATUS "")