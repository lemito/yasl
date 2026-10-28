// Backend of mem::sys for a user-supplied allocator (jemalloc, mimalloc, ...).
// Define these four functions in your program and link them in:
//
//   void*  yasl_sys_alloc(size_t size, size_t align, size_t* real_size);
//   void   yasl_sys_free(void* ptr, size_t size, size_t align);
//   void*  yasl_sys_resize(void* ptr, size_t old_size, size_t want,
//                          size_t align, size_t* real_size);  // nullptr =
//                          cannot
//   size_t yasl_sys_good_size(size_t size);
#include "sys.h"

extern "C" {
void* yasl_sys_alloc(size_t size, size_t align, size_t* real_size);
void yasl_sys_free(void* ptr, size_t size, size_t align);
void* yasl_sys_resize(void* ptr, size_t old_size, size_t want, size_t align,
                      size_t* real_size);
size_t yasl_sys_good_size(size_t size);
}

namespace yasl::mem::sys {
Block alloc(size_t size, size_t align) {
  size_t real = 0;
  void* p = ::yasl_sys_alloc(size, align, &real);
  return p ? Block{p, real} : Block{};
}

void free(void* ptr, size_t size, size_t align) noexcept {
  ::yasl_sys_free(ptr, size, align);
}

Block resize(void* ptr, size_t old_size, size_t want, size_t align) {
  size_t real = 0;
  void* p = ::yasl_sys_resize(ptr, old_size, want, align, &real);
  return p ? Block{p, real} : Block{};
}

size_t good_size(size_t size) noexcept { return ::yasl_sys_good_size(size); }
}  // namespace yasl::mem::sys
