#pragma once

#include <yasl/core/bits.h>
#include <yasl/core/utility.h>
#include <yasl/mem/resource.h>
#include <yasl/vocab/optional.h>

#include <compare>  // operator<=> result types: language support

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
#include <emmintrin.h>
#define YASL_ALGO_SSE2 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define YASL_ALGO_NEON 1
#endif

namespace yasl {
namespace detail {
inline const unsigned char* find_byte_simd(const unsigned char* first,
                                           size_t n, unsigned char value) {
#if defined(YASL_ALGO_SSE2)
  const __m128i needle = _mm_set1_epi8(static_cast<char>(value));
  while (n >= 16) {
    const __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i*>(first));
    const int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(block, needle));
    if (mask != 0) return first + __builtin_ctz(static_cast<unsigned>(mask));
    first += 16;
    n -= 16;
  }
#elif defined(YASL_ALGO_NEON)
  const uint8x16_t needle = vdupq_n_u8(value);
  while (n >= 16) {
    const uint8x16_t block = vld1q_u8(first);
    const uint8x16_t eq = vceqq_u8(block, needle);
    alignas(16) unsigned char mask[16];
    vst1q_u8(mask, eq);
    for (size_t i = 0; i < 16; ++i) if (mask[i] != 0) return first + i;
    first += 16;
    n -= 16;
  }
#endif
  for (size_t i = 0; i < n; ++i) if (first[i] == value) return first + i;
  return nullptr;
}

inline size_t count_byte_simd(const unsigned char* first, size_t n,
                              unsigned char value) {
  size_t result = 0;
#if defined(YASL_ALGO_SSE2)
  const __m128i needle = _mm_set1_epi8(static_cast<char>(value));
  while (n >= 16) {
    const __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i*>(first));
    result += static_cast<size_t>(__builtin_popcount(static_cast<unsigned>(
        _mm_movemask_epi8(_mm_cmpeq_epi8(block, needle)))));
    first += 16;
    n -= 16;
  }
#elif defined(YASL_ALGO_NEON)
  const uint8x16_t needle = vdupq_n_u8(value);
  while (n >= 16) {
    const uint8x16_t eq = vceqq_u8(vld1q_u8(first), needle);
    alignas(16) unsigned char mask[16];
    vst1q_u8(mask, eq);
    for (unsigned char bit : mask) result += bit != 0;
    first += 16;
    n -= 16;
  }
#endif
  for (size_t i = 0; i < n; ++i) result += first[i] == value;
  return result;
}

inline bool equal_byte_simd(const unsigned char* a, const unsigned char* b,
                           size_t n) {
#if defined(YASL_ALGO_SSE2)
  while (n >= 16) {
    const __m128i x = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a));
    const __m128i y = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
    if (_mm_movemask_epi8(_mm_cmpeq_epi8(x, y)) != 0xffff) return false;
    a += 16; b += 16; n -= 16;
  }
#elif defined(YASL_ALGO_NEON)
  while (n >= 16) {
    const uint8x16_t eq = vceqq_u8(vld1q_u8(a), vld1q_u8(b));
    alignas(16) unsigned char mask[16];
    vst1q_u8(mask, eq);
    for (unsigned char bit : mask) if (bit == 0) return false;
    a += 16; b += 16; n -= 16;
  }
#endif
  for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) return false;
  return true;
}
}  // namespace detail
#if defined(YASL_ALGO_SSE2)
#undef YASL_ALGO_SSE2
#endif
#if defined(YASL_ALGO_NEON)
#undef YASL_ALGO_NEON
#endif

// Basic sequence algorithms.  The pointer overloads deliberately use the
// compiler builtins: on each target these lower to the best available libc
// (often SIMD) implementation, while the constexpr/iterator paths remain
// completely portable.
template <class In, class Out>
constexpr Out copy(In first, In last, Out out) {
  for (; first != last; ++first, ++out) {
    *out = *first;
  }
  return out;
}

template <class T>
constexpr T* copy(const T* first, const T* last, T* out)
    requires(TriviallyCopyable<T>) {
  size_t n = size_t(last - first);
  if (!is_constant_evaluated() && n != 0) {
    mem_copy(out, first, n * sizeof(T));
    return out + n;
  }
  for (size_t i = 0; i < n; ++i) out[i] = first[i];
  return out + n;
}

template <class In, class Size, class Out>
constexpr Out copy_n(In first, Size n, Out out) {
  for (Size i = 0; i < n; ++i, ++first, ++out) *out = *first;
  return out;
}

template <class T>
constexpr T* copy_n(const T* first, size_t n, T* out)
    requires(TriviallyCopyable<T>) {
  if (!is_constant_evaluated() && n != 0) mem_copy(out, first, n * sizeof(T));
  else for (size_t i = 0; i < n; ++i) out[i] = first[i];
  return out + n;
}

template <class B, class E, class O>
constexpr O copy_backward(B first, E last, O out) {
  while (first != last) *--out = *--last;
  return out;
}

template <class T>
constexpr T* copy_backward(const T* first, const T* last, T* out)
    requires(TriviallyCopyable<T>) {
  size_t n = size_t(last - first);
  T* result = out - n;
  if (!is_constant_evaluated() && n != 0) mem_move(result, first, n * sizeof(T));
  else for (size_t i = n; i != 0; --i) result[i - 1] = first[i - 1];
  return result;
}

template <class In, class Out>
constexpr Out move(In first, In last, Out out) {
  for (; first != last; ++first, ++out) *out = yasl::move(*first);
  return out;
}

template <class B, class E, class O>
constexpr O move_backward(B first, E last, O out) {
  while (first != last) *--out = yasl::move(*--last);
  return out;
}

template <class It, class T>
constexpr void fill(It first, It last, const T& value) {
  for (; first != last; ++first) *first = value;
}

template <class T>
constexpr void fill(T* first, T* last, const T& value)
    requires(TriviallyCopyable<T>) {
  size_t n = size_t(last - first);
  // memset is valid for byte-sized objects and is substantially faster than
  // a scalar loop.  Other types use the generic assignment semantics.
  if constexpr (sizeof(T) == 1) {
    if (!is_constant_evaluated() && n != 0) {
      mem_set(first, static_cast<unsigned char>(value), n);
      return;
    }
  }
  for (size_t i = 0; i < n; ++i) first[i] = value;
}

template <class It, class Size, class T>
constexpr It fill_n(It first, Size n, const T& value) {
  for (Size i = 0; i < n; ++i, ++first) *first = value;
  return first;
}

template <class T>
constexpr T* fill_n(T* first, size_t n, const T& value)
    requires(__is_trivially_copyable(T)) {
  yasl::fill(first, first + n, value);
  return first + n;
}

template <class It, class V>
constexpr It find(It first, It last, const V& value) {
  for (; first != last; ++first) if (*first == value) break;
  return first;
}

template <class T>
constexpr T* find(T* first, T* last, const T& value)
    requires(SameAs<T, char> || SameAs<T, signed char> ||
             SameAs<T, unsigned char>) {
  size_t n = size_t(last - first);
  if (!is_constant_evaluated() && n != 0) {
    const unsigned char* p = detail::find_byte_simd(
        reinterpret_cast<const unsigned char*>(first), n,
        static_cast<unsigned char>(value));
    return p ? reinterpret_cast<T*>(const_cast<unsigned char*>(p)) : last;
  }
  for (; first != last; ++first) if (*first == value) break;
  return first;
}

template <class It, class Pred>
It find_if_not(It first, It last, Pred pred) {
  for (; first != last && pred(*first); ++first) {}
  return first;
}

template <class It, class V>
size_t count(It first, It last, const V& value) {
  size_t n = 0;
  for (; first != last; ++first) n += (*first == value) ? 1 : 0;
  return n;
}

template <class T>
size_t count(T* first, T* last, const T& value)
    requires(SameAs<T, char> || SameAs<T, signed char> ||
             SameAs<T, unsigned char>) {
  if (!is_constant_evaluated()) {
    return detail::count_byte_simd(reinterpret_cast<const unsigned char*>(first),
                                   size_t(last - first),
                                   static_cast<unsigned char>(value));
  }
  size_t n = 0;
  for (; first != last; ++first) n += *first == value;
  return n;
}

template <class It1, class It2>
constexpr bool equal(It1 first1, It1 last1, It2 first2) {
  for (; first1 != last1; ++first1, ++first2)
    if (!(*first1 == *first2)) return false;
  return true;
}

template <class T>
constexpr bool equal(const T* first, const T* last, const T* other)
    requires(SameAs<T, char> || SameAs<T, signed char> ||
             SameAs<T, unsigned char>) {
  size_t n = size_t(last - first);
  if (!is_constant_evaluated()) {
    return detail::equal_byte_simd(reinterpret_cast<const unsigned char*>(first),
                                   reinterpret_cast<const unsigned char*>(other), n);
  }
  for (size_t i = 0; i < n; ++i) if (first[i] != other[i]) return false;
  return true;
}

template <class It, class Out>
constexpr Out reverse_copy(It first, It last, Out out) {
  while (first != last) *out++ = *--last;
  return out;
}

template <class In, class Out, class F>
constexpr Out transform(In first, In last, Out out, F fn) {
  for (; first != last; ++first, ++out) *out = fn(*first);
  return out;
}

template <class It, class T>
constexpr void replace(It first, It last, const T& old_value,
                       const T& new_value) {
  for (; first != last; ++first)
    if (*first == old_value) *first = new_value;
}

template <class It, class Pred, class T>
constexpr void replace_if(It first, It last, Pred pred, const T& new_value) {
  for (; first != last; ++first)
    if (pred(*first)) *first = new_value;
}
// First position in [first, last) where !cmp(*it, value).
template <class It, class V, class Cmp = Less>
constexpr It lower_bound(It first, It last, const V& value, Cmp cmp = Cmp()) {
  auto n = last - first;
  while (n > 0) {
    auto half = n / 2;
    It mid = first + half;
    if (cmp(*mid, value)) {
      first = mid + 1;
      n -= half + 1;
    } else {
      n = half;
    }
  }
  return first;
}

// First position in [first, last) where cmp(value, *it).
template <class It, class V, class Cmp = Less>
constexpr It upper_bound(It first, It last, const V& value, Cmp cmp = Cmp()) {
  auto n = last - first;
  while (n > 0) {
    auto half = n / 2;
    It mid = first + half;
    if (!cmp(value, *mid)) {
      first = mid + 1;
      n -= half + 1;
    } else {
      n = half;
    }
  }
  return first;
}

template <class T>
constexpr void reverse(T* first, T* last) {
  while (first < last) {
    --last;
    yasl::swap(*first, *last);
    ++first;
  }
}

// Rotates [first, last) so that `mid` becomes the first element.
template <class T>
constexpr void rotate(T* first, T* mid, T* last) {
  yasl::reverse(first, mid);
  yasl::reverse(mid, last);
  yasl::reverse(first, last);
}

template <class A, class B>
constexpr bool equal(const A* a, size_t na, const B* b, size_t nb) {
  if (na != nb) {
    return false;
  }
  for (size_t i = 0; i < na; ++i) {
    if (!(a[i] == b[i])) {
      return false;
    }
  }
  return true;
}

// Lexicographic three-way comparison of two sequences.
template <class It1, class It2>
constexpr auto compare3(It1 a, It1 a_end, It2 b,
                        It2 b_end) -> decltype(*a <=> *b) {
  for (; a != a_end && b != b_end; ++a, ++b) {
    if (auto c = *a <=> *b; c != 0) {
      return c;
    }
  }
  if (a != a_end) {
    return std::strong_ordering::greater;
  }
  if (b != b_end) {
    return std::strong_ordering::less;
  }
  return std::strong_ordering::equal;
}

namespace detail {
template <class It, class Cmp>
void insertion_sort(It first, It last, Cmp& cmp) {
  if (first == last) {
    return;
  }
  for (It i = first + 1; i != last; ++i) {
    auto v = yasl::move(*i);
    It j = i;
    while (j != first && cmp(v, *(j - 1))) {
      *j = yasl::move(*(j - 1));
      --j;
    }
    *j = yasl::move(v);
  }
}

template <class It, class Cmp>
void sift_down(It first, size_t root, size_t n, Cmp& cmp) {
  auto v = yasl::move(first[root]);
  for (;;) {
    size_t child = 2 * root + 1;
    if (child >= n) {
      break;
    }
    if (child + 1 < n && cmp(first[child], first[child + 1])) {
      ++child;
    }
    if (!cmp(v, first[child])) {
      break;
    }
    first[root] = yasl::move(first[child]);
    root = child;
  }
  first[root] = yasl::move(v);
}

template <class It, class Cmp>
void heap_sort(It first, It last, Cmp& cmp) {
  size_t n = size_t(last - first);
  for (size_t i = n / 2; i-- > 0;) {
    sift_down(first, i, n, cmp);
  }
  for (size_t end = n; end-- > 1;) {
    yasl::swap(first[0], first[end]);
    sift_down(first, 0, end, cmp);
  }
}

// Moves the median of first[1], middle, last[-1] to *first and partitions
// around it. Needs at least 3 elements. Returns the cut: [first, cut) <= pivot
// <= [cut, last).
template <class It, class Cmp>
It partition_pivot(It first, It last, Cmp& cmp) {
  It a = first + 1;
  It b = first + (last - first) / 2;
  It c = last - 1;
  if (cmp(*a, *b)) {
    if (cmp(*b, *c)) {
      yasl::swap(*first, *b);
    } else if (cmp(*a, *c)) {
      yasl::swap(*first, *c);
    } else {
      yasl::swap(*first, *a);
    }
  } else if (cmp(*a, *c)) {
    yasl::swap(*first, *a);
  } else if (cmp(*b, *c)) {
    yasl::swap(*first, *c);
  } else {
    yasl::swap(*first, *b);
  }
  It lo = first + 1;
  It hi = last;
  for (;;) {
    while (cmp(*lo, *first)) {
      ++lo;
    }
    --hi;
    while (cmp(*first, *hi)) {
      --hi;
    }
    if (!(lo < hi)) {
      return lo;
    }
    yasl::swap(*lo, *hi);
    ++lo;
  }
}

template <class It, class Cmp>
void introsort_loop(It first, It last, int depth, Cmp& cmp) {
  while (last - first > 16) {
    if (depth == 0) {
      heap_sort(first, last, cmp);
      return;
    }
    --depth;
    It cut = partition_pivot(first, last, cmp);
    introsort_loop(cut, last, depth, cmp);
    last = cut;
  }
  insertion_sort(first, last, cmp);
}

template <class It, class Cmp, class V>
void merge_sort(It first, It last, V* buf, Cmp& cmp) {
  size_t n = size_t(last - first);
  if (n <= 16) {
    insertion_sort(first, last, cmp);
    return;
  }
  It mid = first + n / 2;
  merge_sort(first, mid, buf, cmp);
  merge_sort(mid, last, buf, cmp);
  size_t n1 = size_t(mid - first);
  for (size_t i = 0; i < n1; ++i) {
    yasl::construct_at(buf + i, yasl::move(first[i]));
  }
  size_t i = 0;
  It j = mid;
  It out = first;
  while (i < n1 && j != last) {
    if (cmp(*j, buf[i])) {
      *out = yasl::move(*j);
      ++j;
    } else {
      *out = yasl::move(buf[i]);
      ++i;
    }
    ++out;
  }
  while (i < n1) {
    *out = yasl::move(buf[i]);
    ++i;
    ++out;
  }
  yasl::destroy_n(buf, n1);
}
}  // namespace detail

// Unstable O(n log n) sort (introsort: median-of-3 quicksort, heapsort
// fallback, insertion sort for small ranges).
template <class It, class Cmp = Less>
void sort(It first, It last, Cmp cmp = Cmp()) {
  size_t n = size_t(last - first);
  if (n < 2) {
    return;
  }
  detail::introsort_loop(first, last, 2 * (bit_width(uint64(n)) - 1), cmp);
}

// Stable O(n log n) merge sort; the temporary buffer (n/2 elements) comes from
// `res`.
template <class It, class Cmp = Less>
void stable_sort(It first, It last, Cmp cmp = Cmp(),
                 mem::Resource* res = mem::default_resource()) {
  using V = RemoveCVRef<decltype(*first)>;
  size_t n = size_t(last - first);
  if (n < 2) {
    return;
  }
  if (n <= 16) {
    detail::insertion_sort(first, last, cmp);
    return;
  }
  V* buf = res->allocate_array<V>(n / 2);
  try {
    detail::merge_sort(first, last, buf, cmp);
  } catch (...) {
    res->deallocate_array(buf, n / 2);
    throw;
  }
  res->deallocate_array(buf, n / 2);
}

// Sorts by key(element) with "<"; key is evaluated on every comparison.
template <class It, class Key>
void sort_by_key(It first, It last, Key key) {
  yasl::sort(first, last,
             [&](const auto& a, const auto& b) { return key(a) < key(b); });
}

// Index of an element equivalent to `value` in the sorted range, or none.
template <class It, class V, class Cmp = Less>
Optional<size_t> binary_search(It first, It last, const V& value,
                               Cmp cmp = Cmp()) {
  It it = yasl::lower_bound(first, last, value, cmp);
  if (it != last && !cmp(value, *it)) {
    return size_t(it - first);
  }
  return none;
}

// Moves elements satisfying pred to the front (not stable); returns the first
// one that does not.
template <class It, class Pred>
It partition(It first, It last, Pred pred) {
  for (;;) {
    while (first != last && pred(*first)) {
      ++first;
    }
    if (first == last) {
      return first;
    }
    --last;
    while (first != last && !pred(*last)) {
      --last;
    }
    if (first == last) {
      return first;
    }
    yasl::swap(*first, *last);
    ++first;
  }
}

// Puts the element that would be at `nth` in sorted order there; smaller ones
// before it, larger after.
template <class It, class Cmp = Less>
void nth_element(It first, It nth, It last, Cmp cmp = Cmp()) {
  if (first == last || nth == last) {
    return;
  }
  int depth = 2 * (bit_width(uint64(last - first)) - 1);
  while (last - first > 3) {
    if (depth == 0) {
      detail::heap_sort(first, last, cmp);
      return;
    }
    --depth;
    It cut = detail::partition_pivot(first, last, cmp);
    if (cut <= nth) {
      first = cut;
    } else {
      last = cut;
    }
  }
  detail::insertion_sort(first, last, cmp);
}

template <class It, class Cmp = Less>
It min_element(It first, It last, Cmp cmp = Cmp()) {
  It best = first;
  if (first == last) {
    return last;
  }
  for (++first; first != last; ++first) {
    if (cmp(*first, *best)) {
      best = first;
    }
  }
  return best;
}

template <class It, class Cmp = Less>
It max_element(It first, It last, Cmp cmp = Cmp()) {
  It best = first;
  if (first == last) {
    return last;
  }
  for (++first; first != last; ++first) {
    if (cmp(*best, *first)) {
      best = first;
    }
  }
  return best;
}

template <class It, class Pred>
It find_if(It first, It last, Pred pred) {
  for (; first != last; ++first) {
    if (pred(*first)) {
      break;
    }
  }
  return first;
}

template <class It, class Pred>
bool any_of(It first, It last, Pred pred) {
  return yasl::find_if(first, last, pred) != last;
}

template <class It, class Pred>
bool all_of(It first, It last, Pred pred) {
  for (; first != last; ++first) {
    if (!pred(*first)) {
      return false;
    }
  }
  return true;
}

template <class It, class Pred>
bool none_of(It first, It last, Pred pred) {
  return !yasl::any_of(first, last, pred);
}

template <class It, class Pred>
size_t count_if(It first, It last, Pred pred) {
  size_t n = 0;
  for (; first != last; ++first) {
    n += pred(*first) ? 1 : 0;
  }
  return n;
}
}  // namespace yasl
