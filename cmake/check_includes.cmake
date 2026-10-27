# Enforces the "no standard library" policy for the yasl core.
#
# Allowed: language-support headers the compiler needs (<new>, <initializer_list>,
# <compare>, <cstddef>, <cstdint>), <atomic>, and OS / C runtime headers.
# yasl/compat/ (std interop), tests and the test runner are exempt.

set(ALLOWED
    cstddef cstdint stddef.h stdint.h new initializer_list compare atomic
    sys/types.h sys/stat.h unistd.h fcntl.h errno.h string.h stdlib.h pthread.h)

file(GLOB_RECURSE FILES "${ROOT}/yasl/*.h" "${ROOT}/yasl/*.cpp")
set(BAD 0)
foreach(F ${FILES})
    if(F MATCHES "/yasl/compat/" OR F MATCHES "_test\\.cpp$" OR F MATCHES "/yasl/test/")
        continue()
    endif()
    file(STRINGS "${F}" LINES REGEX "^[ \t]*#[ \t]*include[ \t]*<")
    foreach(L ${LINES})
        string(REGEX REPLACE "^[^<]*<([^>]+)>.*$" "\\1" H "${L}")
        if(H MATCHES "^yasl/")
            continue()
        endif()
        list(FIND ALLOWED "${H}" IDX)
        if(IDX EQUAL -1)
            file(RELATIVE_PATH REL "${ROOT}" "${F}")
            message(SEND_ERROR "${REL}: forbidden include <${H}>")
            set(BAD 1)
        endif()
    endforeach()
endforeach()
if(NOT BAD)
    message(STATUS "include policy: OK")
endif()
