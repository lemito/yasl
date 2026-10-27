#include "obj_pool.h"

namespace yasl::mem {
StringView ObjPool::copy_str(StringView s) {
  char* p = static_cast<char*>(arena_.allocate(s.size() + 1, 1));
  yasl::mem_copy(p, s.data(), s.size());
  p[s.size()] = '\0';
  return {p, s.size()};
}
}  // namespace yasl::mem
