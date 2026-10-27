#pragma once

#include <yasl/core/check.h>
#include <yasl/core/utility.h>

// Element-sequence helpers shared by Vector and SmallVector. They work on a
// plain (data, size) pair: capacity and allocation stay with the container.

namespace yasl {
// An iterator/sentinel pair we can walk once.
template <class It, class S>
concept IteratorRange = requires(It it, S s) {
  *it;
  ++it;
  { it != s } -> ConvertibleTo<bool>;
};

namespace detail {
// Moves n objects from src into raw storage at dst and destroys the originals.
// On exception dst is cleaned up and src is left untouched.
template <class T>
void relocate(T* src, size_t n, T* dst) {
  if constexpr (TriviallyCopyable<T>) {
    if (n) {
      yasl::mem_copy(static_cast<void*>(dst), static_cast<const void*>(src),
                     n * sizeof(T));
    }
  } else {
    size_t i = 0;
    try {
      for (; i < n; ++i) {
        if constexpr (NothrowMoveConstructible<T> || !CopyConstructible<T>) {
          yasl::construct_at(dst + i, yasl::move(src[i]));
        } else {
          yasl::construct_at(dst + i, static_cast<const T&>(src[i]));
        }
      }
    } catch (...) {
      yasl::destroy_n(dst, i);
      throw;
    }
    yasl::destroy_n(src, n);
  }
}

// Copy-constructs [first, last) into raw storage at dst; cleans up if a copy
// throws.
template <class T, class It, class S>
void copy_construct(It first, S last, T* dst) {
  T* cur = dst;
  try {
    for (; first != last; ++first, ++cur) {
      yasl::construct_at(cur, *first);
    }
  } catch (...) {
    yasl::destroy(dst, cur);
    throw;
  }
}

template <class T>
void fill_construct(T* dst, size_t n, const T& value) {
  size_t i = 0;
  try {
    for (; i < n; ++i) {
      yasl::construct_at(dst + i, value);
    }
  } catch (...) {
    yasl::destroy_n(dst, i);
    throw;
  }
}

// Inserts `value` before index idx < size. Needs room for one more element.
template <class T>
void insert_inside(T* data, size_t& size, size_t idx, T&& value) {
  YASL_ASSERT(idx < size);
  yasl::construct_at(data + size, yasl::move(data[size - 1]));
  ++size;
  for (size_t i = size - 2; i > idx; --i) {
    data[i] = yasl::move(data[i - 1]);
  }
  data[idx] = yasl::move(value);
}

// Removes [first, last).
template <class T>
void erase_range(T* data, size_t& size, size_t first, size_t last) {
  YASL_ASSERT(first <= last && last <= size);
  if (first == last) {
    return;
  }
  T* out = data + first;
  for (T* in = data + last; in != data + size; ++in, ++out) {
    *out = yasl::move(*in);
  }
  yasl::destroy(out, data + size);
  size -= last - first;
}

// Removes the elements for which pred holds; returns how many.
template <class T, class Pred>
size_t erase_if(T* data, size_t& size, Pred& pred) {
  T* out = data;
  for (T* in = data; in != data + size; ++in) {
    if (!pred(*in)) {
      if (out != in) {
        *out = yasl::move(*in);
      }
      ++out;
    }
  }
  size_t removed = size_t(data + size - out);
  erase_range(data, size, size_t(out - data), size);
  return removed;
}

// Drops an element when same(previous kept, element) holds.
template <class T, class Same>
void dedup_by(T* data, size_t& size, Same& same) {
  if (size < 2) {
    return;
  }
  T* out = data;
  for (T* in = data + 1; in != data + size; ++in) {
    if (!same(static_cast<const T&>(*out), static_cast<const T&>(*in))) {
      ++out;
      if (out != in) {
        *out = yasl::move(*in);
      }
    }
  }
  erase_range(data, size, size_t(out - data) + 1, size);
}
}  // namespace detail
}  // namespace yasl
