#pragma once

#include "resource.h"

namespace yasl::mem {
// Segregated free-list allocator: power-of-two size classes from 8 bytes up to
// `max_block`, carved out of large slabs. Freed blocks are reused immediately.
// Bigger (or over-aligned) requests go straight to upstream.
//
// Slabs are returned to upstream on release() / destruction.
// Not thread-safe; wrap in Synchronized<Pool> if needed.
class Pool : public Resource {
 public:
  struct Options {
    size_t max_block = 4096;
    size_t slab_size = 64 * 1024;
  };

  explicit Pool(Resource* upstream = default_resource()) noexcept
      : Pool(upstream, Options{}) {}

  Pool(Resource* upstream, Options opts) noexcept;

  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;

  ~Pool() override { release(); }

  // Frees every slab. Outstanding small blocks become invalid.
  void release() noexcept;

  Resource* upstream() const noexcept { return upstream_; }

 protected:
  Block do_alloc(size_t size, size_t align) override;
  void do_free(void* ptr, size_t size, size_t align) noexcept override;

 private:
  static constexpr size_t kMinBlock = 8;
  static constexpr size_t kMaxClasses = 28;
  static constexpr size_t kNoClass = size_t(-1);

  struct FreeNode {
    FreeNode* next;
  };

  struct Slab {
    Slab* next;
    size_t size;
  };

  struct SizeClass {
    FreeNode* free = nullptr;
    char* cur = nullptr;
    char* end = nullptr;
  };

  size_t class_of(size_t size, size_t align) const noexcept;
  void refill(SizeClass& c, size_t block);

  Resource* upstream_;
  Options opts_;
  Slab* slabs_ = nullptr;
  SizeClass classes_[kMaxClasses];
};
}  // namespace yasl::mem
