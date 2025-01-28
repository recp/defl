cmake_minimum_required(VERSION 3.13)
project(defl
  VERSION 0.1.0
  HOMEPAGE_URL https://github.com/recp/defl
  DESCRIPTION "fast & flexible deflate library"
  LANGUAGES C
)

# Dependencies && Submodules
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
# Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

# huff package
add_subdirectory(deps/huff)

add_library(defl
    src/infl/infl.c
)

include(GNUInstallDirs)

target_include_directories(defl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(defl
    PUBLIC huff::huff
)

install(TARGETS defl
        EXPORT deflTargets
        LIBRARY DESTINATION  ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION  ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION  ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT deflTargets
        FILE deflTargets.cmake
        NAMESPACE defl::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/defl)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})

if(NOT CPack_CMake_INCLUDED)
  include(CPack)
endif()
