if(NOT DEFINED ROOT OR NOT DEFINED CXX)
    message(FATAL_ERROR "ROOT and CXX are required")
endif()

set(HEADERS
    yasl/core/math_raw.h
    yasl/time/time.h
    yasl/os/env.h
)
set(TMP_DIR "/tmp/yasl-public-headers")
file(MAKE_DIRECTORY "${TMP_DIR}")
set(BAD 0)
foreach(H ${HEADERS})
    string(REPLACE "/" "_" STEM "${H}")
    set(SRC "${TMP_DIR}/${STEM}.cpp")
    file(WRITE "${SRC}" "#include <${H}>\nint main() { return 0; }\n")
    execute_process(
        COMMAND "${CXX}" -std=c++20 "-I${ROOT}" -fsyntax-only "${SRC}"
        RESULT_VARIABLE RC
        OUTPUT_VARIABLE OUT
        ERROR_VARIABLE ERR)
    if(NOT RC EQUAL 0)
        message(SEND_ERROR "public header ${H} does not compile standalone:\n${OUT}${ERR}")
        set(BAD 1)
    endif()
endforeach()
if(NOT BAD)
    message(STATUS "public headers: OK")
endif()
