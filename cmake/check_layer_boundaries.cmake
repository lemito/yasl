if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

# Keep low-level headers independent from the high-level formatting and
# concurrency layers.  These are intentionally explicit rules: adding a new
# exception requires an architectural decision rather than silently widening
# the dependency cone.
set(RULES
    "yasl/core/math_raw.h|yasl/vocab/|yasl/io/|yasl/conc/"
    "yasl/time/time.h|yasl/io/output.h|yasl/io/"
    "yasl/os/env.cpp|yasl/io/file.h|yasl/io/"
)
set(BAD 0)
foreach(R ${RULES})
    string(REPLACE "|" ";" PARTS "${R}")
    list(GET PARTS 0 FILE)
    list(GET PARTS 1 NEEDLE)
    list(GET PARTS 2 LABEL)
    file(STRINGS "${ROOT}/${FILE}" CONTENT REGEX "^[ \\t]*#[ \\t]*include")
    string(JOIN "\\n" CONTENT_TEXT ${CONTENT})
    string(FIND "${CONTENT_TEXT}" "${NEEDLE}" POS)
    if(NOT POS EQUAL -1)
        message(SEND_ERROR "${FILE} has forbidden layer dependency ${LABEL}")
        set(BAD 1)
    endif()
endforeach()
if(NOT BAD)
    message(STATUS "layer boundaries: OK")
endif()
