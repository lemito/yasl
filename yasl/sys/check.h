#pragma once

namespace yasl::detail {
    [[noreturn]] void assert_fail(const char* expr, const char* file, int line, const char* func) noexcept;
}

// Debug-only invariant check. Enabled unless NDEBUG is set (or forced with YASL_FORCE_ASSERTS).
#if defined(NDEBUG) && !defined(YASL_FORCE_ASSERTS)
    #define YASL_ASSERT(expr) ((void)0)
#else
    #define YASL_ASSERT(expr) \
        (static_cast<bool>(expr) ? (void)0 : ::yasl::detail::assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif
