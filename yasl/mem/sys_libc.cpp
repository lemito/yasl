// Backend of mem::sys on the C runtime. Platform headers live only here.
#include <stdlib.h>

#include "sys.h"

#if defined(_WIN32)
#include <malloc.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

namespace yasl::mem::sys {
namespace {
bool is_plain(size_t align) noexcept { return align <= kMaxAlign; }

size_t usable(void* p, size_t requested, size_t align) noexcept {
  (void)requested;
  (void)p;
  (void)align;
#if defined(_WIN32)
  return is_plain(align) ? ::_msize(p) : ::_aligned_msize(p, align, 0);
#elif defined(__APPLE__)
  return ::malloc_size(p);
#elif defined(__GLIBC__)
  return ::malloc_usable_size(p);
#else
  return requested;  // musl and others: no way to ask
#endif
}
}  // namespace

Block alloc(size_t size, size_t align) {
  if (size == 0) {
    size = 1;
  }
  void* p = nullptr;
#if defined(_WIN32)
  p = is_plain(align) ? ::malloc(size) : ::_aligned_malloc(size, align);
#else
  if (is_plain(align)) {
    p = ::malloc(size);
  } else if (::posix_memalign(&p, align, size) != 0) {
    p = nullptr;
  }
#endif
  if (!p) {
    return {};
  }
  return {p, usable(p, size, align)};
}

void free(void* ptr, size_t, size_t align) noexcept {
#if defined(_WIN32)
  is_plain(align) ? ::free(ptr) : ::_aligned_free(ptr);
#else
  (void)align;
  ::free(ptr);
#endif
}

Block resize(void* ptr, size_t, size_t want, size_t align) {
  if (want == 0) {
    want = 1;
  }
  void* p = nullptr;
#if defined(_WIN32)
  p = is_plain(align) ? ::realloc(ptr, want)
                      : ::_aligned_realloc(ptr, want, align);
#else
  if (!is_plain(align)) {
    return {};  // realloc does not keep over-alignment
  }
  p = ::realloc(ptr, want);
#endif
  if (!p) {
    return {};
  }
  return {p, usable(p, want, align)};
}

size_t good_size(size_t size) noexcept {
#if defined(__GLIBC__) && !defined(_WIN32)
  // 8 bytes of chunk header, 16-byte granularity, 24-byte minimum.
  size_t chunk = (size + 8 + 15) & ~size_t(15);
  return chunk < 32 ? 24 : chunk - 8;
#else
  return size == 0 ? 1 : size;
#endif
}
}  // namespace yasl::mem::sys
