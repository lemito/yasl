#include <new>

#include "sys.h"

extern "C" {
struct __sized_ptr_t {
  void* p;
  size_t n;
};
__sized_ptr_t tcmalloc_size_returning_operator_new_nothrow(
    size_t size) noexcept;
__sized_ptr_t tcmalloc_size_returning_operator_new_aligned_nothrow(
    size_t size, std::align_val_t alignment) noexcept;
size_t nallocx(size_t size, int flags) noexcept;
}

namespace yasl::mem::sys {
Block alloc(size_t size, size_t align) {
  if (size == 0) {
    size = 1;
  }
  __sized_ptr_t r =
      align <= kMaxAlign
          ? ::tcmalloc_size_returning_operator_new_nothrow(size)
          : ::tcmalloc_size_returning_operator_new_aligned_nothrow(
                size, std::align_val_t(align));
  if (!r.p) {
    return {};
  }
  return {r.p, r.n};
}

void free(void* ptr, size_t size, size_t align) noexcept {
  if (size == 0) {
    size = 1;
  }
  if (align <= kMaxAlign) {
    ::operator delete(ptr, size);
  } else {
    ::operator delete(ptr, size, std::align_val_t(align));
  }
}

Block resize(void*, size_t, size_t, size_t) { return {}; }

size_t good_size(size_t size) noexcept {
  return ::nallocx(size == 0 ? 1 : size, 0);
}
}  // namespace yasl::mem::sys
