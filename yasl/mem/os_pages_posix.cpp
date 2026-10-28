#if !defined(_WIN32)
#include <sys/mman.h>
#include <unistd.h>

#include "os_pages.h"

namespace yasl::mem::detail {
size_t os_page_size() noexcept {
  long n = ::sysconf(_SC_PAGESIZE);
  return n > 0 ? size_t(n) : 4096;
}

void* os_map(size_t n) noexcept {
  void* p = ::mmap(nullptr, n, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p == MAP_FAILED ? nullptr : p;
}

void os_unmap(void* p, size_t n) noexcept { ::munmap(p, n); }

void* os_remap(void* p, size_t old_n, size_t new_n) noexcept {
#if defined(__linux__)
  void* q = ::mremap(p, old_n, new_n, MREMAP_MAYMOVE);
  return q == MAP_FAILED ? nullptr : q;
#else
  (void)p;
  (void)old_n;
  (void)new_n;
  return nullptr;
#endif
}

void os_advise_huge(void* p, size_t n) noexcept {
#if defined(__linux__) && defined(MADV_HUGEPAGE)
  ::madvise(p, n, MADV_HUGEPAGE);
#else
  (void)p;
  (void)n;
#endif
}
}  // namespace yasl::mem::detail
#endif
