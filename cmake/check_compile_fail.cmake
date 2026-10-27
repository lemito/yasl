# Compile-time format string checks: ok_*.cpp must compile, bad_*.cpp must not.
# Needs -DCXX=<compiler> -DROOT=<source dir>.

file(GLOB CASES "${ROOT}/cmake/compile_fail/*.cpp")
set(FAILED 0)
foreach(F ${CASES})
    get_filename_component(NAME "${F}" NAME_WE)
    execute_process(
        COMMAND ${CXX} -std=c++20 -fsyntax-only -I${ROOT} ${F}
        RESULT_VARIABLE RC
        ERROR_VARIABLE ERR
        OUTPUT_QUIET)
    if(NAME MATCHES "^ok_" AND NOT RC EQUAL 0)
        message(SEND_ERROR "${NAME}: expected to compile:\n${ERR}")
        set(FAILED 1)
    elseif(NAME MATCHES "^bad_" AND RC EQUAL 0)
        message(SEND_ERROR "${NAME}: expected a compile error")
        set(FAILED 1)
    endif()
endforeach()
if(NOT FAILED)
    list(LENGTH CASES N)
    message(STATUS "compile-fail checks: OK (${N} cases)")
endif()
