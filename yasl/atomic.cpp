#include "atomic.h"

static_assert(std::atomic<size_t>::is_always_lock_free, "yasl::RefCount requires lock-free size_t atomics");
