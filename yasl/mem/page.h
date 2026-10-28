#pragma once

#include "compose.h"

namespace yasl::mem {
// Pages straight from the OS (mmap / VirtualAlloc), for big blocks. A
// composition block, see compose.h.
//
//   * sizes are rounded up to whole pages, and the reported size says so;
//   * freshly mapped memory is zero;
//   * resize() moves the mapping with mremap on Linux (so growing a big array
//     copies no bytes and touches no pages); elsewhere it answers "cannot";
//   * alignment above kMaxPageAlign is not served ({}).
//
// owns() cannot tell pages from other memory and always says yes.
struct Pages {
  // Every OS page size is a multiple of this.
  static constexpr size_t kMaxPageAlign = 4096;

  Block alloc(size_t size, size_t align) noexcept;
  void free(void* p, size_t size, size_t align) noexcept;
  Block resize(void* p, size_t old_size, size_t want, size_t align) noexcept;

  bool owns(const void*) const noexcept { return true; }
};

// Page size of the OS.
size_t page_size() noexcept;

// Ask the OS to back big mappings with huge pages (MADV_HUGEPAGE on Linux;
// large pages need a privilege on Windows and are not used; no effect on
// macOS). Affects blocks allocated afterwards.
void set_huge_pages(bool on) noexcept;
}  // namespace yasl::mem
