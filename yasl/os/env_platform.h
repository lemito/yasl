#pragma once

#include <yasl/cont/seq/vector.h>
#include <yasl/str/str.h>

namespace yasl::env::detail {
// Platform backend for discovering process arguments.  The public env API
// owns the storage and controls initialization; this function only appends.
void append_process_args(Vector<String>& out);
}  // namespace yasl::env::detail
