// Backend of mem::sys on gperftools' tcmalloc.
#include <gperftools/tcmalloc.h>

#include "sys.h"

namespace yasl::mem::sys {
Block alloc(size_t size, size_t align) {
  if (size == 0) {
    size = 1;
  }
  void* p = align <= kMaxAlign ? ::tc_malloc(size) : ::tc_memalign(align, size);
  if (!p) {
    return {};
  }
  return {p, ::tc_malloc_size(p)};
}

void free(void* ptr, size_t size, size_t align) noexcept {
  (void)align;
  ::tc_free_sized(ptr, size == 0 ? 1 : size);
}

Block resize(void* ptr, size_t, size_t want, size_t align) {
  if (align > kMaxAlign) {
    return {};
  }
  void* p = ::tc_realloc(ptr, want == 0 ? 1 : want);
  if (!p) {
    return {};
  }
  return {p, ::tc_malloc_size(p)};
}

size_t good_size(size_t size) noexcept {
  return ::tc_nallocx(size == 0 ? 1 : size, 0);
}
}  // namespace yasl::mem::sys
