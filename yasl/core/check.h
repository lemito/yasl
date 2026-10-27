#pragma once

namespace yasl {
// What a failed check or panic() tells the panic hook.
struct PanicInfo {
  const char* kind;     // "assertion failed", "verification failed", "insist
                        // failed", "panic"
  const char* expr;     // the checked expression, or nullptr
  const char* message;  // explanation, or nullptr
  const char* file;
  int line;
  const char* func;
};

using PanicHook = void (*)(const PanicInfo&);

// Replaces the hook that runs first on every panic and returns the previous one
// (nullptr = none). A hook normally throws (tests turn panics into exceptions);
// if it returns, the process prints the message and aborts.
PanicHook set_panic_hook(PanicHook hook) noexcept;

// Prints "file:line: func: kind: expr: message" to stderr, followed by a
// backtrace where the platform has one, and aborts. It does not allocate or
// format through yasl::format, so it is safe to call when memory has run out.
[[noreturn]] void panic(const PanicInfo& info);

namespace detail {
[[noreturn]] void panic_at(const char* kind, const char* expr,
                           const char* message, const char* file, int line,
                           const char* func);

[[noreturn]] void assert_fail(const char* expr, const char* file, int line,
                              const char* func);
}  // namespace detail
}  // namespace yasl

// Debug-only invariant check. Enabled unless NDEBUG is set (or forced with
// YASL_FORCE_ASSERTS).
#if defined(NDEBUG) && !defined(YASL_FORCE_ASSERTS)
#define YASL_ASSERT(expr) ((void)0)
#else
#define YASL_ASSERT(expr)  \
  (static_cast<bool>(expr) \
       ? (void)0           \
       : ::yasl::detail::assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif

// Always-on check without a message.
#define YASL_VERIFY(expr)                                                \
  (static_cast<bool>(expr)                                               \
       ? (void)0                                                         \
       : ::yasl::detail::panic_at("verification failed", #expr, nullptr, \
                                  __FILE__, __LINE__, __func__))

// Always-on check with an explanation (a string literal or C string).
#define YASL_INSIST(expr, message)                                             \
  (static_cast<bool>(expr)                                                     \
       ? (void)0                                                               \
       : ::yasl::detail::panic_at("insist failed", #expr, (message), __FILE__, \
                                  __LINE__, __func__))

// Unconditional panic.
#define YASL_PANIC(message)                                                 \
  ::yasl::detail::panic_at("panic", nullptr, (message), __FILE__, __LINE__, \
                           __func__)
