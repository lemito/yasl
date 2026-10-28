#pragma once

#include "resource.h"

// The system allocator behind every yasl resource. One backend is compiled in
// (CMake option YASL_MALLOC): libc (default), tcmalloc, or custom, where the
// user supplies the extern "C" functions declared in sys_custom.cpp.
namespace yasl::mem::sys {
// {nullptr, 0} on failure. `size` may be 0; the real size is returned.
Block alloc(size_t size, size_t align);

// `size` and `align` must be the ones given to alloc (or anything between the
// requested and the reported size). `align` selects the matching free routine.
void free(void* ptr, size_t size, size_t align) noexcept;

// Tries to change the block size in place or by a bitwise move. {nullptr, 0}
// means "cannot": the block is untouched and the caller does alloc+move+free.
// The caller guarantees the contents are trivially relocatable.
Block resize(void* ptr, size_t old_size, size_t want, size_t align);

// The size the allocator would really give for a request of `size` bytes.
size_t good_size(size_t size) noexcept;
}  // namespace yasl::mem::sys
