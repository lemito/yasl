LIBRARY()

VERSION(0.1.0)

NO_UTIL()

ADDINCL(GLOBAL contrib/libs/yasl)

SRCS(
    yasl/conc/futex.cpp
    yasl/conc/mutex.cpp
    yasl/conc/ref_count.cpp
    yasl/conc/thread.cpp
    yasl/core/check.cpp
    yasl/core/types.cpp
    yasl/io/buffer.cpp
    yasl/io/file.cpp
    yasl/io/fmt.cpp
    yasl/io/fs.cpp
    yasl/io/input.cpp
    yasl/io/output.cpp
    yasl/mem/arena.cpp
    yasl/mem/obj_pool.cpp
    yasl/mem/page.cpp
    yasl/mem/pool.cpp
    yasl/mem/resource.cpp
    yasl/mem/thread_local_pool.cpp
    yasl/os/env.cpp
    yasl/os/fd.cpp
    yasl/ptr/shared.cpp
    yasl/rand/random.cpp
    yasl/str/num.cpp
    yasl/str/str.cpp
    yasl/time/time.cpp
)

IF (YASL_MALLOC == "tcmalloc")
    SRCS(yasl/mem/sys_tcmalloc.cpp)
    CFLAGS(GLOBAL -DYASL_MALLOC_TCMALLOC)
ELSEIF (YASL_MALLOC == "custom")
    SRCS(yasl/mem/sys_custom.cpp)
ELSE()
    SRCS(yasl/mem/sys_libc.cpp)
ENDIF()

IF (OS_WINDOWS)
    SRCS(yasl/mem/os_pages_win.cpp)
ELSE()
    SRCS(yasl/mem/os_pages_posix.cpp)
ENDIF()

IF (YASL_DEFAULT_ALLOC == "chain")
    CFLAGS(-DYASL_DEFAULT_ALLOC_CHAIN)
ENDIF()

END()

RECURSE(
    yasl/test
)
