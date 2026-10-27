#include "check.h"

#include <stdlib.h>
#include <unistd.h>

#include <atomic>

#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define YASL_HAS_BACKTRACE 1
#endif

namespace yasl {
namespace {
std::atomic<PanicHook> g_hook{nullptr};

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

void put_line(int line) noexcept {
  char num[16];
  char* p = num + sizeof(num);
  *--p = '\0';
  unsigned v = line > 0 ? unsigned(line) : 0;
  do {
    *--p = char('0' + v % 10);
    v /= 10;
  } while (v);
  put(p);
}
}  // namespace

PanicHook set_panic_hook(PanicHook hook) noexcept {
  return g_hook.exchange(hook);
}

void panic(const PanicInfo& info) {
  if (PanicHook hook = g_hook.load()) {
    hook(info);
  }
  put(info.file);
  put(":");
  put_line(info.line);
  put(": ");
  put(info.func);
  put(": ");
  put(info.kind);
  if (info.expr) {
    put(": ");
    put(info.expr);
  }
  if (info.message) {
    put(": ");
    put(info.message);
  }
  put("\n");
#ifdef YASL_HAS_BACKTRACE
  void* frames[64];
  int n = ::backtrace(frames, 64);
  ::backtrace_symbols_fd(frames, n, 2);
#endif
  abort();
}

namespace detail {
void panic_at(const char* kind, const char* expr, const char* message,
              const char* file, int line, const char* func) {
  panic(PanicInfo{kind, expr, message, file, line, func});
}

void assert_fail(const char* expr, const char* file, int line,
                 const char* func) {
  panic_at("assertion failed", expr, nullptr, file, line, func);
}
}  // namespace detail
}  // namespace yasl
