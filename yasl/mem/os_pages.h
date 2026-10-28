#pragma once

#include <yasl/core/types.h>

// Per-OS primitives behind mem::Pages (os_pages_posix.cpp, os_pages_win.cpp).
namespace yasl::mem::detail {
size_t os_page_size() noexcept;

// Zeroed read/write pages; nullptr on failure. `n` is a multiple of the page
// size.
void* os_map(size_t n) noexcept;

void os_unmap(void* p, size_t n) noexcept;

// Moves/resizes a mapping; nullptr = this OS cannot (the mapping is untouched).
void* os_remap(void* p, size_t old_n, size_t new_n) noexcept;

void os_advise_huge(void* p, size_t n) noexcept;
}  // namespace yasl::mem::detail
