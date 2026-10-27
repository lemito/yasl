#include "random.h"

#include "hash.h"

#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>

namespace yasl {
    uint64 os_random_u64() noexcept {
        uint64 v = 0;
        if (::getrandom(&v, sizeof(v), 0) == ssize_t(sizeof(v))) {
            return v;
        }
        int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            ssize_t n = ::read(fd, &v, sizeof(v));
            ::close(fd);
            if (n == ssize_t(sizeof(v))) {
                return v;
            }
        }
        // Last resort: ASLR and the address of a local.
        return mix64(uint64(reinterpret_cast<uintptr_t>(&v)) ^ uint64(reinterpret_cast<uintptr_t>(&os_random_u64)));
    }

    uint64 process_seed() noexcept {
        static const uint64 seed = os_random_u64();
        return seed;
    }
}
