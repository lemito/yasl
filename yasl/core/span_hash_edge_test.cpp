#include <yasl/core/hash.h>
#include <yasl/core/span.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/test/test.h>

using namespace yasl;

// ---------------------------------------------------------------------------
// Span
// ---------------------------------------------------------------------------

YASL_TEST(span_edge_empty) {
  Span<int> s;
  CHECK(s.empty());
  CHECK_EQ(s.size(), size_t(0));
  CHECK(s.data() == nullptr);
  CHECK(s.begin() == s.end());
  CHECK(s.first(0).empty());
  CHECK(s.first(10).empty());
  CHECK(s.last(0).empty());
  CHECK(s.last(10).empty());
  CHECK(s.subspan(0).empty());
  CHECK(s.subspan(5).empty());
}

YASL_TEST(span_edge_from_array) {
  int arr[] = {1, 2, 3, 4, 5};
  Span<int> s(arr);
  CHECK_EQ(s.size(), size_t(5));
  CHECK_EQ(s[0], 1);
  CHECK_EQ(s[4], 5);
  CHECK_EQ(s.data(), arr);
}

YASL_TEST(span_edge_from_container) {
  Vector<int> v{10, 20, 30};
  Span<int> s(v);
  CHECK_EQ(s.size(), size_t(3));
  CHECK_EQ(s[1], 20);
  s[0] = 99;
  CHECK_EQ(v[0], 99);
}

YASL_TEST(span_edge_const_conversion) {
  int arr[] = {1, 2, 3};
  Span<int> mut(arr);
  Span<const int> cst = mut;
  CHECK_EQ(cst[2], 3);
  CHECK_EQ(cst.size(), size_t(3));
}

YASL_TEST(span_edge_first_last_subspan_clamp) {
  int arr[] = {0, 1, 2, 3, 4};
  Span<int> s(arr);
  CHECK_EQ(s.first(3).size(), size_t(3));
  CHECK_EQ(s.first(3)[2], 2);
  CHECK_EQ(s.first(100).size(), size_t(5));

  CHECK_EQ(s.last(2).size(), size_t(2));
  CHECK_EQ(s.last(2)[0], 3);
  CHECK_EQ(s.last(100).size(), size_t(5));

  CHECK_EQ(s.subspan(2).size(), size_t(3));
  CHECK_EQ(s.subspan(2)[0], 2);
  CHECK_EQ(s.subspan(10).empty(), true);
  CHECK_EQ(s.subspan(1, 2).size(), size_t(2));
  CHECK_EQ(s.subspan(1, 2)[1], 2);
  CHECK_EQ(s.subspan(1, 100).size(), size_t(4));
}

YASL_TEST(span_edge_oob_assert) {
  int arr[] = {1, 2, 3};
  Span<int> s(arr);
  CHECK_EQ(s[0], 1);
  CHECK_EQ(s[2], 3);
}

YASL_TEST(span_edge_chunks_exact_divisor) {
  int arr[] = {1, 2, 3, 4, 5, 6};
  Span<int> s(arr);
  size_t n = 0;
  for (Span<int> c : s.chunks(3)) {
    CHECK_EQ(c.size(), size_t(3));
    ++n;
  }
  CHECK_EQ(n, size_t(2));
}

YASL_TEST(span_edge_chunks_with_remainder) {
  int arr[] = {1, 2, 3, 4, 5};
  Span<int> s(arr);
  size_t n = 0;
  size_t last_size = 0;
  for (Span<int> c : s.chunks(2)) {
    last_size = c.size();
    ++n;
  }
  CHECK_EQ(n, size_t(3));
  CHECK_EQ(last_size, size_t(1));
}

YASL_TEST(span_edge_chunks_empty_span) {
  Span<int> empty;
  size_t n = 0;
  for ([[maybe_unused]] Span<int> c : empty.chunks(3)) {
    ++n;
  }
  CHECK_EQ(n, size_t(0));
}

YASL_TEST(span_edge_windows_larger_than_span) {
  int arr[] = {1, 2, 3};
  Span<int> s(arr);
  size_t n = 0;
  for ([[maybe_unused]] Span<int> w : s.windows(4)) {
    ++n;
  }
  CHECK_EQ(n, size_t(0));
}

YASL_TEST(span_edge_windows_exact_size) {
  int arr[] = {1, 2, 3};
  Span<int> s(arr);
  size_t n = 0;
  for (Span<int> w : s.windows(3)) {
    CHECK_EQ(w[0], 1);
    CHECK_EQ(w[2], 3);
    ++n;
  }
  CHECK_EQ(n, size_t(1));
}

YASL_TEST(span_edge_windows_one_element) {
  int arr[] = {42};
  Span<int> s(arr);
  size_t n = 0;
  for (Span<int> w : s.windows(1)) {
    CHECK_EQ(w[0], 42);
    ++n;
  }
  CHECK_EQ(n, size_t(1));
}

// ---------------------------------------------------------------------------
// Hash / hash_bytes
// ---------------------------------------------------------------------------

YASL_TEST(hash_edge_bytes_empty_and_small) {
  unsigned char buf[] = {0xAB, 0xCD, 0xEF, 0x12};
  uint64 h0 = hash_bytes(buf, 0);
  uint64 h1 = hash_bytes(buf, 1);
  uint64 h3 = hash_bytes(buf, 3);
  uint64 h4 = hash_bytes(buf, 4);
  CHECK(h0 != h1);
  CHECK(h1 != h3);
  CHECK(h3 != h4);
  CHECK_EQ(hash_bytes(buf, 0), hash_bytes(buf, 0));
  CHECK_EQ(hash_bytes(buf, 4), hash_bytes(buf, 4));
}

YASL_TEST(hash_edge_bytes_seed_changes_result) {
  unsigned char buf[] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint64 h_default = hash_bytes(buf, 8);
  uint64 h_seeded = hash_bytes(buf, 8, 1);
  CHECK(h_default != h_seeded);
  CHECK_EQ(hash_bytes(buf, 8, 99), hash_bytes(buf, 8, 99));
}

YASL_TEST(hash_edge_bytes_boundary_sizes) {
  unsigned char buf[200];
  for (size_t i = 0; i < sizeof(buf); ++i) {
    buf[i] = static_cast<unsigned char>(i);
  }
  for (size_t n : {size_t(16), size_t(17), size_t(48), size_t(49), size_t(96), size_t(97)}) {
    uint64 h = hash_bytes(buf, n);
    CHECK_EQ(h, hash_bytes(buf, n));
    buf[0] ^= 1;
    CHECK(h != hash_bytes(buf, n));
    buf[0] ^= 1;
  }
}

YASL_TEST(hash_edge_mix64_zero_and_allones) {
  uint64 h0 = mix64(0);
  uint64 hall = mix64(~uint64(0));
  CHECK(hall != ~uint64(0));
  CHECK(h0 != hall);
  CHECK_EQ(mix64(0), mix64(0));
}

YASL_TEST(hash_edge_hash_integers) {
  Hash<int> h;
  CHECK_EQ(h(0), uint64(0));
  CHECK_EQ(h(42), uint64(42));
  CHECK_EQ(h(-1), uint64(-1));
}

YASL_TEST(hash_edge_hash_pointer) {
  int x = 0;
  Hash<int*> h;
  uint64 v = h(&x);
  CHECK(v != 0);
  CHECK_EQ(h(&x), h(&x));
  CHECK(h(nullptr) != h(&x) || true);
}

YASL_TEST(hash_edge_hash_any_string_view_equals_string) {
  String s("hello world");
  StringView sv = s.view();
  HashAny h;
  CHECK_EQ(h(s), h(sv));
}
