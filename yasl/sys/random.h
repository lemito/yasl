#pragma once

#include "types.h"

namespace yasl {
    // 64 random bits from the OS (getrandom, falling back to /dev/urandom).
    uint64 os_random_u64() noexcept;

    // Random value fixed for the lifetime of the process; seeds the hash tables against HashDoS.
    uint64 process_seed() noexcept;
}
