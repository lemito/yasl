#include "page.h"

#include <atomic>

#include "os_pages.h"

namespace yasl::mem {
namespace {
std::atomic<bool> g_huge{false};
constexpr size_t kHugeThreshold = size_t(2) << 20;

size_t round_to_pages(size_t n) noexcept {
  size_t page = page_size();
  return align_up(n == 0 ? 1 : n, page);
}
}  // namespace

size_t page_size() noexcept {
  static const size_t size = detail::os_page_size();
  return size;
}

void set_huge_pages(bool on) noexcept {
  g_huge.store(on, std::memory_order_relaxed);
}

Block Pages::alloc(size_t size, size_t align) noexcept {
  if (align > kMaxPageAlign || size > max_value<size_t>() - page_size()) {
    return {};
  }
  size_t n = round_to_pages(size);
  void* p = detail::os_map(n);
  if (p && n >= kHugeThreshold && g_huge.load(std::memory_order_relaxed)) {
    detail::os_advise_huge(p, n);
  }
  return p ? Block{p, n} : Block{};
}

void Pages::free(void* p, size_t size, size_t) noexcept {
  detail::os_unmap(p, round_to_pages(size));
}

Block Pages::resize(void* p, size_t old_size, size_t want,
                    size_t align) noexcept {
  if (align > kMaxPageAlign || want > max_value<size_t>() - page_size()) {
    return {};
  }
  size_t old_n = round_to_pages(old_size);
  size_t n = round_to_pages(want);
  if (n == old_n) {
    return {p, n};
  }
  void* q = detail::os_remap(p, old_n, n);
  return q ? Block{q, n} : Block{};
}
}  // namespace yasl::mem
