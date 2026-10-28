#include <yasl/cont/seq/vector.h>
#include <yasl/mem/page.h>
#include <yasl/mem/thread_local_pool.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

#include <cstring>

using namespace yasl;

YASL_TEST(pages_alloc_free_zeroed) {
  mem::Pages pages;
  size_t page = mem::page_size();
  CHECK(page >= 4096);
  mem::Block b = pages.alloc(1 << 20, 4096);
  CHECK(b.ptr != nullptr);
  CHECK_EQ(b.size, size_t(1) << 20);
  CHECK_EQ(reinterpret_cast<uintptr_t>(b.ptr) % page, uintptr_t(0));
  auto* bytes = static_cast<unsigned char*>(b.ptr);
  CHECK_EQ(bytes[0], 0);
  CHECK_EQ(bytes[b.size - 1], 0);
  bytes[b.size - 1] = 9;
  pages.free(b.ptr, 1 << 20, 4096);

  mem::Block odd = pages.alloc(100, 8);  // rounded up to a page
  CHECK_EQ(odd.size, page);
  pages.free(odd.ptr, 100, 8);
  CHECK(pages.alloc(100, 1 << 20).ptr == nullptr);  // too aligned
}

YASL_TEST(pages_resize_keeps_data) {
  mem::Pages pages;
  mem::Block b = pages.alloc(1 << 20, 8);
  CHECK(b.ptr != nullptr);
  std::memset(b.ptr, 0x5A, 1 << 20);
  mem::Block big = pages.resize(b.ptr, 1 << 20, size_t(64) << 20, 8);
#if defined(__linux__)
  CHECK(big.ptr != nullptr);
  CHECK_EQ(big.size, size_t(64) << 20);
  auto* bytes = static_cast<unsigned char*>(big.ptr);
  for (size_t i = 0; i < (1 << 20); i += 4093) {
    CHECK_EQ(bytes[i], 0x5A);
  }
  CHECK_EQ(bytes[(size_t(64) << 20) - 1], 0);  // new pages are zero
  mem::Block small = pages.resize(big.ptr, size_t(64) << 20, 1 << 16, 8);
  CHECK(small.ptr != nullptr);
  CHECK_EQ(static_cast<unsigned char*>(small.ptr)[0], 0x5A);
  pages.free(small.ptr, 1 << 16, 8);
#else
  if (big.ptr) {
    pages.free(big.ptr, size_t(64) << 20, 8);
  } else {
    pages.free(b.ptr, 1 << 20, 8);
  }
#endif
}

YASL_TEST(pages_in_default_chain) {
  mem::Resource* r = mem::chain_resource();
  // Big blocks work through the chain, including growth by Vector.
  Vector<uint64> v(r);
  for (uint64 i = 0; i < 300000; ++i) {
    v.push_back(i);
  }
  CHECK_EQ(v[299999], uint64(299999));
  CHECK_EQ(v[12345], uint64(12345));
  void* p = r->allocate(size_t(1) << 20, 8);
  r->deallocate(p, size_t(1) << 20, 8);
  mem::set_huge_pages(true);
  void* h = r->allocate(size_t(4) << 20, 8);
  static_cast<char*>(h)[0] = 1;
  r->deallocate(h, size_t(4) << 20, 8);
  mem::set_huge_pages(false);
}
