PROGRAM(yasl_tests)

NO_UTIL()

PEERDIR(
    contrib/libs/yasl
)

SRCDIR(contrib/libs/yasl)

SRCS(
    yasl/algo/algo_test.cpp
    yasl/algo/iter_test.cpp
    yasl/compat/compat_test.cpp
    yasl/conc/atomic_test.cpp
    yasl/conc/channel_test.cpp
    yasl/conc/guarded_test.cpp
    yasl/conc/semaphore_test.cpp
    yasl/conc/thread_pool_test.cpp
    yasl/conc/thread_test.cpp
    yasl/cont/adapter/adapters_test.cpp
    yasl/cont/adapter/priority_queue_test.cpp
    yasl/cont/assoc/hash_map_test.cpp
    yasl/cont/assoc/map_test.cpp
    yasl/cont/assoc/radix_trie_test.cpp
    yasl/cont/assoc/string_map_test.cpp
    yasl/cont/assoc/trie_test.cpp
    yasl/cont/seq/bitset_test.cpp
    yasl/cont/seq/deque_test.cpp
    yasl/cont/seq/intrusive_list_test.cpp
    yasl/cont/seq/list_test.cpp
    yasl/cont/seq/small_vector_test.cpp
    yasl/cont/seq/vector_test.cpp
    yasl/core/check_test.cpp
    yasl/core/math_test.cpp
    yasl/io/buffer_test.cpp
    yasl/io/fmt_test.cpp
    yasl/io/fs_test.cpp
    yasl/io/io_test.cpp
    yasl/mem/mem_test.cpp
    yasl/mem/obj_pool_test.cpp
    yasl/mem/page_test.cpp
    yasl/mem/thread_local_pool_test.cpp
    yasl/os/env_test.cpp
    yasl/os/fd_test.cpp
    yasl/ptr/intrusive_test.cpp
    yasl/ptr/shared_test.cpp
    yasl/ptr/unique_test.cpp
    yasl/rand/random_test.cpp
    yasl/str/num_test.cpp
    yasl/str/str_test.cpp
    yasl/str/symbol_test.cpp
    yasl/time/time_test.cpp
    yasl/vocab/defer_test.cpp
    yasl/vocab/function_test.cpp
    yasl/vocab/optional_test.cpp
    yasl/vocab/result_test.cpp
    yasl/vocab/variant_test.cpp
    yasl/test/sys_custom_impl.cpp
    yasl/test/test.cpp
)

IF (YASL_MALLOC == "custom")
    CFLAGS(-DYASL_MALLOC_CUSTOM)
ENDIF()

END()
