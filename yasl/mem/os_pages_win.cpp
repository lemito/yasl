// Written without a Windows toolchain at hand: not compiled or tested yet
// (see the open question about Windows in TODO.md).
#if defined(_WIN32)
#include <windows.h>

#include "os_pages.h"

namespace yasl::mem::detail {
size_t os_page_size() noexcept {
  SYSTEM_INFO info;
  ::GetSystemInfo(&info);
  return info.dwPageSize;
}

void* os_map(size_t n) noexcept {
  return ::VirtualAlloc(nullptr, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void os_unmap(void* p, size_t) noexcept { ::VirtualFree(p, 0, MEM_RELEASE); }

// No mremap: the caller falls back to allocate + copy + free.
void* os_remap(void*, size_t, size_t) noexcept { return nullptr; }

void os_advise_huge(void*, size_t) noexcept {}
}  // namespace yasl::mem::detail
#endif
