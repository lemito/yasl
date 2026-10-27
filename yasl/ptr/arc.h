#pragma once

#include "shared.h"

namespace yasl {
// Old name of Shared, kept for source compatibility.
template <class T>
using Arc [[deprecated("use Shared")]] = Shared<T>;

template <class T, class... Args>
[[deprecated("use make_shared")]] Shared<T> make_arc(Args&&... args) {
  return make_shared<T>(yasl::forward<Args>(args)...);
}

template <class T, class... Args>
[[deprecated("use make_shared_in")]] Shared<T> make_arc_in(mem::Resource* res,
                                                           Args&&... args) {
  return make_shared_in<T>(res, yasl::forward<Args>(args)...);
}
}  // namespace yasl
