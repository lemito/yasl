#pragma once

#include <yasl/core/span.h>
#include <yasl/str/view.h>

namespace yasl::io {
// POSIX file mapping. ReadWrite changes the file; CopyOnWrite keeps changes
// private. Neither mode grows the file. The caller must prevent truncation
// while mapped: accessing pages beyond the new EOF can raise SIGBUS.
enum class MemMapMode { ReadOnly, ReadWrite, CopyOnWrite };

class MemMap {
 public:
  static constexpr uint64 kToEnd = uint64(-1);

  MemMap() noexcept = default;
  explicit MemMap(StringView path, MemMapMode mode = MemMapMode::ReadOnly,
                  uint64 offset = 0, uint64 length = kToEnd);
  // Borrows fd only during this call; the mapping survives closing fd.
  static MemMap map(int fd, MemMapMode mode = MemMapMode::ReadOnly,
                    uint64 offset = 0, uint64 length = kToEnd);

  MemMap(const MemMap&) = delete;
  MemMap& operator=(const MemMap&) = delete;
  MemMap(MemMap&& other) noexcept;
  MemMap& operator=(MemMap&& other) noexcept;
  ~MemMap();

  const char* data() const noexcept {
    return mapped_size_ ? static_cast<const char*>(base_) + head_ : nullptr;
  }
  size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }
  StringView view() const noexcept { return {data(), size_}; }
  Span<const char> bytes() const noexcept { return {data(), size_}; }
  // Throws BadAccess for a read-only mapping, including an empty one.
  char* mut_data();
  Span<char> mut_bytes() { return {mut_data(), size_}; }

  // Synchronous msync of shared writes; no-op for read-only/private/empty
  // mappings. Destruction only unmaps; call flush() to observe writeback
  // errors.
  void flush();
  // Idempotent. Views/pointers become invalid. Reports munmap errors.
  void close();

 private:
  void swap(MemMap& other) noexcept;
  void close_quietly() noexcept;

  void* base_ = nullptr;
  size_t mapped_size_ = 0;
  size_t head_ = 0;
  size_t size_ = 0;
  MemMapMode mode_ = MemMapMode::ReadOnly;
};
}  // namespace yasl::io
