#pragma once

#include "resource.h"

namespace yasl::mem {
// Forwards to upstream and counts what goes through. Great for tests and
// for finding out how much a subsystem really allocates. Not thread-safe.
class Tracking final : public Resource {
 public:
  struct Stats {
    size_t allocations = 0;
    size_t deallocations = 0;
    size_t bytes_in_use = 0;
    size_t peak_bytes = 0;
    size_t total_bytes = 0;
    size_t resizes = 0;  // successful in-place or moving resizes

    size_t live() const noexcept { return allocations - deallocations; }
  };

  explicit Tracking(Resource* upstream = default_resource()) noexcept
      : upstream_(upstream) {}

  const Stats& stats() const noexcept { return stats_; }

  Resource* upstream() const noexcept { return upstream_; }

 protected:
  Block do_alloc(size_t size, size_t align) override {
    Block b = upstream_->allocate_at_least(size, align);
    ++stats_.allocations;
    // Count the requested size: that is what deallocate() usually gets back.
    stats_.bytes_in_use += size;
    stats_.total_bytes += size;
    stats_.peak_bytes = yasl::max(stats_.peak_bytes, stats_.bytes_in_use);
    return b;
  }

  void do_free(void* ptr, size_t size, size_t align) noexcept override {
    ++stats_.deallocations;
    // The caller may pass anything between the requested and the reported size.
    stats_.bytes_in_use -= yasl::min(size, stats_.bytes_in_use);
    upstream_->deallocate(ptr, size, align);
  }

  Block do_resize(void* ptr, size_t old_size, size_t want, size_t align,
                  bool relocatable) override {
    Block b = upstream_->resize(ptr, old_size, want, align, relocatable);
    if (b.ptr) {
      ++stats_.resizes;
      stats_.bytes_in_use -= yasl::min(old_size, stats_.bytes_in_use);
      stats_.bytes_in_use += want;
      stats_.total_bytes += want;
      stats_.peak_bytes = yasl::max(stats_.peak_bytes, stats_.bytes_in_use);
    }
    return b;
  }

 private:
  Resource* upstream_;
  Stats stats_;
};
}  // namespace yasl::mem
