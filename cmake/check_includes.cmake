# Enforces the "no standard library" policy for the yasl core.
#
# Allowed: language-support headers the compiler needs (<new>, <initializer_list>,
# <compare>, <cstddef>, <cstdint>), <atomic>, and OS / C runtime headers.
# execinfo.h: backtrace() in panic(). sys/mman.h: memfd_create()/shm_open() for MemFd. sys/random.h: getrandom() for the per-process hash seed; time.h: clock_gettime() for Instant; sched.h: sched_yield() for this_thread::yield(); dirent.h: opendir() for fs::read_dir; stdio.h: rename() for fs::rename.
# yasl/compat/ (std interop), tests, the test runner and yasl/bench are exempt.

set(ALLOWED
    cstddef cstdint stddef.h stdint.h new initializer_list compare atomic
    sys/types.h sys/stat.h sys/mman.h sys/random.h time.h sched.h dirent.h stdio.h unistd.h fcntl.h errno.h string.h stdlib.h pthread.h execinfo.h)

file(GLOB_RECURSE FILES "${ROOT}/yasl/*.h" "${ROOT}/yasl/*.cpp")
set(BAD 0)
foreach(F ${FILES})
    if(F MATCHES "/yasl/compat/" OR F MATCHES "_test\\.cpp$" OR F MATCHES "/yasl/test/" OR F MATCHES "/yasl/bench/")
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
