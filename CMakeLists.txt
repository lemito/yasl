cmake_minimum_required(VERSION 3.20)
project(yasl LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(YASL_BUILD_TESTS "Build yasl tests" ${PROJECT_IS_TOP_LEVEL})
option(YASL_SANITIZE "Build with AddressSanitizer + UBSan" OFF)

find_package(Threads REQUIRED)

file(GLOB_RECURSE YASL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/yasl/*.cpp)
list(FILTER YASL_SOURCES EXCLUDE REGEX "_test\\.cpp$")
list(FILTER YASL_SOURCES EXCLUDE REGEX "/yasl/test/")

add_library(yasl STATIC ${YASL_SOURCES})
add_library(yasl::yasl ALIAS yasl)
target_include_directories(yasl PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_compile_features(yasl PUBLIC cxx_std_20)
target_link_libraries(yasl PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(yasl PRIVATE /W4)
else()
    target_compile_options(yasl PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(YASL_SANITIZE)
    target_compile_options(yasl PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(yasl PUBLIC -fsanitize=address,undefined)
endif()

if(YASL_BUILD_TESTS)
    enable_testing()
    file(GLOB_RECURSE YASL_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/yasl/*_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/yasl/test/*.cpp)
    add_executable(yasl_tests ${YASL_TEST_SOURCES})
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
