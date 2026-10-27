#include "check.h"

#include <stdlib.h>
#include <unistd.h>

namespace yasl::detail {
    namespace {
        void put(const char* s) noexcept {
            size_t n = __builtin_strlen(s);
            while (n > 0) {
                ssize_t w = ::write(2, s, n);
                if (w <= 0) {
                    return;
                }
                s += w;
                n -= size_t(w);
            }
        }
    }

    void assert_fail(const char* expr, const char* file, int line, const char* func) noexcept {
        char num[16];
        char* p = num + sizeof(num);
        *--p = '\0';
        unsigned v = line > 0 ? unsigned(line) : 0;
        do {
            *--p = char('0' + v % 10);
            v /= 10;
        } while (v);

        put(file);
        put(":");
        put(p);
        put(": ");
        put(func);
        put(": assertion failed: ");
        put(expr);
        put("\n");
        abort();
    }
}
