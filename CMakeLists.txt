cmake_minimum_required(VERSION 3.20)
project(yasl VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(YASL_BUILD_TESTS "Build yasl tests" ${PROJECT_IS_TOP_LEVEL})
option(YASL_BUILD_BENCH "Build the benchmarks (yasl/bench)" OFF)
option(YASL_SANITIZE "Build with AddressSanitizer + UBSan" OFF)

set(YASL_MALLOC "libc" CACHE STRING "System allocator behind yasl::mem: libc, tcmalloc or custom")
set_property(CACHE YASL_MALLOC PROPERTY STRINGS libc tcmalloc custom)
if(NOT YASL_MALLOC MATCHES "^(libc|tcmalloc|custom)$")
    message(FATAL_ERROR "YASL_MALLOC must be libc, tcmalloc or custom (got '${YASL_MALLOC}')")
endif()

set(YASL_DEFAULT_ALLOC "sys" CACHE STRING "Default resource: sys (system allocator) or chain (thread-local pools for small blocks)")
set_property(CACHE YASL_DEFAULT_ALLOC PROPERTY STRINGS sys chain)
if(NOT YASL_DEFAULT_ALLOC MATCHES "^(sys|chain)$")
    message(FATAL_ERROR "YASL_DEFAULT_ALLOC must be sys or chain (got '${YASL_DEFAULT_ALLOC}')")
endif()

find_package(Threads REQUIRED)

file(GLOB_RECURSE YASL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/yasl/*.cpp)
list(FILTER YASL_SOURCES EXCLUDE REGEX "_test\\.cpp$")
list(FILTER YASL_SOURCES EXCLUDE REGEX "/yasl/test/")
list(FILTER YASL_SOURCES EXCLUDE REGEX "/yasl/bench/")
# Exactly one mem::sys backend is compiled.
list(FILTER YASL_SOURCES EXCLUDE REGEX "/yasl/mem/sys_(libc|tcmalloc|custom)\\.cpp$")
list(APPEND YASL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/yasl/mem/sys_${YASL_MALLOC}.cpp)

add_library(yasl STATIC ${YASL_SOURCES})
add_library(yasl::yasl ALIAS yasl)
target_include_directories(yasl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_compile_features(yasl PUBLIC cxx_std_20)
target_link_libraries(yasl PUBLIC Threads::Threads)

if(YASL_MALLOC STREQUAL "tcmalloc")
    find_library(YASL_TCMALLOC_LIB NAMES tcmalloc_minimal tcmalloc)
    if(NOT YASL_TCMALLOC_LIB)
        message(FATAL_ERROR "YASL_MALLOC=tcmalloc needs gperftools (e.g. apt install libgoogle-perftools-dev)")
    endif()
    target_link_libraries(yasl PUBLIC ${YASL_TCMALLOC_LIB})
endif()

if(YASL_DEFAULT_ALLOC STREQUAL "chain")
    target_compile_definitions(yasl PRIVATE YASL_DEFAULT_ALLOC_CHAIN)
endif()

if(MSVC)
    target_compile_options(yasl PRIVATE /W4)
else()
    target_compile_options(yasl PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(YASL_SANITIZE)
    target_compile_options(yasl PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(yasl PUBLIC -fsanitize=address,undefined)
endif()

# Installation: find_package(yasl) then link yasl::yasl.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS yasl EXPORT yaslTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY yasl DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
    PATTERN "test" EXCLUDE)
install(EXPORT yaslTargets NAMESPACE yasl:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/yasl)
configure_package_config_file(cmake/yaslConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/yaslConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/yasl)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/yaslConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/yaslConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/yaslConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/yasl)

if(YASL_BUILD_TESTS)
    enable_testing()
    file(GLOB_RECURSE YASL_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/yasl/*_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/yasl/test/*.cpp)
    add_executable(yasl_tests ${YASL_TEST_SOURCES})
    if(YASL_MALLOC STREQUAL "custom")
        target_compile_definitions(yasl_tests PRIVATE YASL_MALLOC_CUSTOM)
    endif()
    target_link_libraries(yasl_tests PRIVATE yasl)
    if(NOT MSVC)
        target_compile_options(yasl_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME yasl_tests COMMAND yasl_tests)
    add_test(NAME yasl_include_policy
        COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_includes.cmake)
    add_test(NAME yasl_format_compile_fail
        COMMAND ${CMAKE_COMMAND} -DCXX=${CMAKE_CXX_COMPILER} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_fail.cmake)
endif()

# Benchmarks: not part of ctest.
if(YASL_BUILD_BENCH)
    add_executable(yasl_bench yasl/bench/bench.cpp)
    target_link_libraries(yasl_bench PRIVATE yasl)
endif()
