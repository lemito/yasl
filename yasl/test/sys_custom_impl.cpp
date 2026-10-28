// A trivial YASL_MALLOC=custom backend, so the option can be tested.
#ifdef YASL_MALLOC_CUSTOM
#include <stddef.h>
#include <stdlib.h>

extern "C" {
void* yasl_sys_alloc(size_t size, size_t align, size_t* real_size) {
  void* p = nullptr;
  if (size == 0) {
    size = 1;
  }
  if (align <= alignof(::max_align_t)) {
    p = ::malloc(size);
  } else if (::posix_memalign(&p, align, size) != 0) {
    p = nullptr;
  }
  *real_size = size;
  return p;
}

void yasl_sys_free(void* ptr, size_t, size_t) { ::free(ptr); }

void* yasl_sys_resize(void*, size_t, size_t, size_t, size_t*) {
  return nullptr;
}

size_t yasl_sys_good_size(size_t size) { return size; }
}
#endif
